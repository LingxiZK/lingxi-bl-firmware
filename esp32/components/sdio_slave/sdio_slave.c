#include "sdio_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "SDIO_SLAVE"

/* 图像帧转发 */
#include "wifi_stream.h"

/* STM32N6 SDIO 包格式 (用于图像帧检测) */
#define SDIO_PKT_MAGIC      0xA5
#define SDIO_PKT_IMAGE_FRAME 0x08   /* 匹配 STM32N6 的 sdio_packet_t.type */

/* SDIO 发送/接收缓冲区 */
static uint8_t s_tx_buffer[SDIO_SLAVE_BUF_COUNT][SDIO_SLAVE_BLOCK_SIZE];
static uint8_t s_rx_buffer[SDIO_SLAVE_BUF_COUNT][SDIO_SLAVE_BLOCK_SIZE];
static QueueHandle_t s_tx_queue = NULL;
static QueueHandle_t s_rx_queue = NULL;
static SemaphoreHandle_t s_sdio_mutex = NULL;

/* 回调 */
static sdio_slave_callback_t s_event_cb = NULL;
static void *s_event_arg = NULL;

/* 统计 */
static sdio_slave_stats_t s_stats = {0};
static SemaphoreHandle_t s_stats_mutex = NULL;

/* 初始化标志 */
static bool s_initialized = false;

/*=============================================================================
 * 内部函数
 *===========================================================================*/

static void sdio_event_handler(uint32_t event)
{
    if (s_event_cb) {
        switch (event) {
            case SDIO_SLAVE_EVT_HOST_RESET:
                s_event_cb(SDIO_EVT_HOST_RESET, s_event_arg);
                break;
            case SDIO_SLAVE_EVT_HOST_ENABLE:
                s_event_cb(SDIO_EVT_HOST_ENABLE, s_event_arg);
                break;
            case SDIO_SLAVE_EVT_SEND_DONE:
                s_event_cb(SDIO_EVT_TX_COMPLETE, s_event_arg);
                break;
            case SDIO_SLAVE_EVT_RECV_DONE:
                s_event_cb(SDIO_EVT_RX_READY, s_event_arg);
                break;
            default:
                s_event_cb(SDIO_EVT_ERROR, s_event_arg);
                break;
        }
    }
}

static void sdio_rx_task(void *arg)
{
    (void)arg;
    sdio_slave_buf_handle_t buf_handle;
    lx_frame_t frame;

    while (1) {
        /* 等待接收完成 */
        esp_err_t ret = sdio_slave_recv(&buf_handle, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Recv error: %d", ret);
            xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
            s_stats.rx_errors++;
            xSemaphoreGive(s_stats_mutex);
            continue;
        }

        /* 获取数据 */
        size_t len = sdio_slave_get_recv_len(buf_handle);
        if (len < LX_FRAME_MIN_LEN || len > LX_FRAME_MAX_LEN) {
            ESP_LOGW(TAG, "Invalid frame length: %d", len);
            sdio_slave_recv_load_buf(buf_handle);
            xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
            s_stats.rx_errors++;
            xSemaphoreGive(s_stats_mutex);
            continue;
        }

        /* 读取原始数据用于检测 */
        uint8_t raw_buf[LX_FRAME_MAX_LEN];
        size_t raw_len = (len < sizeof(raw_buf)) ? len : sizeof(raw_buf);
        sdio_slave_recv_copy(buf_handle, raw_buf, raw_len, NULL, portMAX_DELAY);

        /* ── 检测图像帧分片 (STM32N6 sdio_packet_t 格式) ── */
        if (raw_len >= 3 && raw_buf[0] == SDIO_PKT_MAGIC &&
            raw_buf[1] == SDIO_PKT_IMAGE_FRAME) {
            /* 转发到 Wi-Fi 流 (原始数据作为分片) */
            wifi_stream_forward_fragment(raw_buf, (uint16_t)raw_len, false);
            sdio_slave_recv_load_buf(buf_handle);
            xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
            s_stats.rx_frames++;
            s_stats.rx_bytes += raw_len;
            xSemaphoreGive(s_stats_mutex);
            ESP_LOGD(TAG, "Image frag forwarded, len=%d", raw_len);
            continue;
        }

        /* ── 普通帧 → lx_frame_t 解析 ── */
        ret = sdio_slave_recv_copy(buf_handle, &frame, len, NULL, portMAX_DELAY);
        sdio_slave_recv_load_buf(buf_handle);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Copy error: %d", ret);
            xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
            s_stats.rx_errors++;
            xSemaphoreGive(s_stats_mutex);
            continue;
        }

        /* 验证帧 */
        if (!lx_frame_validate(&frame)) {
            ESP_LOGW(TAG, "Frame validation failed");
            xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
            s_stats.rx_errors++;
            xSemaphoreGive(s_stats_mutex);
            continue;
        }

        /* 放入接收队列 */
        if (xQueueSend(s_rx_queue, &frame, 0) != pdPASS) {
            ESP_LOGW(TAG, "RX queue full, dropping frame");
            xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
            s_stats.rx_errors++;
            xSemaphoreGive(s_stats_mutex);
            continue;
        }

        xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
        s_stats.rx_frames++;
        s_stats.rx_bytes += len;
        xSemaphoreGive(s_stats_mutex);

        ESP_LOGD(TAG, "RX frame type=0x%02X, len=%d", frame.header.type, len);
    }
}

static void sdio_tx_task(void *arg)
{
    (void)arg;
    lx_frame_t frame;

    while (1) {
        if (xQueueReceive(s_tx_queue, &frame, portMAX_DELAY) != pdPASS) {
            continue;
        }

        uint16_t total_len = lx_frame_total_len(&frame);
        if (total_len > SDIO_SLAVE_BLOCK_SIZE) {
            /* 大帧分片发送 */
            ESP_LOGW(TAG, "Frame too large, fragment not implemented");
            continue;
        }

        /* 对齐到块大小 */
        uint16_t send_len = (total_len + SDIO_SLAVE_BLOCK_SIZE - 1) &
                            ~(SDIO_SLAVE_BLOCK_SIZE - 1);

        esp_err_t ret = sdio_slave_send_packet(&frame, send_len, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Send error: %d", ret);
            xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
            s_stats.tx_errors++;
            xSemaphoreGive(s_stats_mutex);
            continue;
        }

        xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
        s_stats.tx_frames++;
        s_stats.tx_bytes += total_len;
        xSemaphoreGive(s_stats_mutex);

        ESP_LOGD(TAG, "TX frame type=0x%02X, len=%d", frame.header.type, total_len);
    }
}

/*=============================================================================
 * API 实现
 *===========================================================================*/

esp_err_t sdio_slave_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 初始化缓冲区 */
    for (int i = 0; i < SDIO_SLAVE_BUF_COUNT; i++) {
        memset(s_tx_buffer[i], 0, SDIO_SLAVE_BLOCK_SIZE);
        memset(s_rx_buffer[i], 0, SDIO_SLAVE_BLOCK_SIZE);
    }

    /* 创建队列 */
    s_tx_queue = xQueueCreate(SDIO_SLAVE_TX_QUEUE, sizeof(lx_frame_t));
    s_rx_queue = xQueueCreate(SDIO_SLAVE_RX_QUEUE, sizeof(lx_frame_t));
    if (s_tx_queue == NULL || s_rx_queue == NULL) {
        ESP_LOGE(TAG, "Queue creation failed");
        return ESP_ERR_NO_MEM;
    }

    /* 创建互斥锁 */
    s_sdio_mutex = xSemaphoreCreateMutex();
    s_stats_mutex = xSemaphoreCreateMutex();
    if (s_sdio_mutex == NULL || s_stats_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex creation failed");
        return ESP_ERR_NO_MEM;
    }

    /* 配置 SDIO Slave */
    sdio_slave_config_t config = {
        .sending_mode       = SDIO_SLAVE_SEND_PACKET,
        .send_queue_size    = SDIO_SLAVE_TX_QUEUE,
        .recv_buffer_size   = SDIO_SLAVE_BLOCK_SIZE,
        .event_cb           = sdio_event_handler,
    };

    esp_err_t ret = sdio_slave_initialize(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDIO slave init failed: %d", ret);
        return ret;
    }

    /* 注册接收缓冲区 */
    for (int i = 0; i < SDIO_SLAVE_BUF_COUNT; i++) {
        ret = sdio_slave_recv_register_buf(s_rx_buffer[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Register buffer %d failed: %d", i, ret);
            sdio_slave_deinit();
            return ret;
        }
    }

    /* 启动接收 */
    for (int i = 0; i < SDIO_SLAVE_BUF_COUNT; i++) {
        sdio_slave_recv_load_buf(sdio_slave_recv_get_buf(NULL, 0));
    }

    /* 创建收发任务 */
    xTaskCreate(sdio_rx_task, "sdio_rx", 4096, NULL, 10, NULL);
    xTaskCreate(sdio_tx_task, "sdio_tx", 4096, NULL, 10, NULL);

    /* 配置中断引脚 */
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << SDIO_SLAVE_IRQ_GPIO),
        .pull_down_en = 0,
        .pull_up_en   = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(SDIO_SLAVE_IRQ_GPIO, 0);

    /* 配置复位/使能引脚为输入 */
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << SDIO_SLAVE_RST_GPIO) |
                           (1ULL << SDIO_SLAVE_EN_GPIO);
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    s_initialized = true;
    ESP_LOGI(TAG, "SDIO Slave initialized");
    return ESP_OK;
}

esp_err_t sdio_slave_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    sdio_slave_stop();
    sdio_slave_deinit();

    if (s_tx_queue) {
        vQueueDelete(s_tx_queue);
        s_tx_queue = NULL;
    }
    if (s_rx_queue) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }
    if (s_sdio_mutex) {
        vSemaphoreDelete(s_sdio_mutex);
        s_sdio_mutex = NULL;
    }
    if (s_stats_mutex) {
        vSemaphoreDelete(s_stats_mutex);
        s_stats_mutex = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "SDIO Slave deinitialized");
    return ESP_OK;
}

esp_err_t sdio_slave_register_callback(sdio_slave_callback_t cb, void *arg)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_event_cb = cb;
    s_event_arg = arg;
    return ESP_OK;
}

esp_err_t sdio_slave_send_frame(const lx_frame_t *frame, int32_t timeout_ms)
{
    if (!s_initialized || frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY :
                       pdMS_TO_TICKS(timeout_ms);

    if (xQueueSend(s_tx_queue, frame, ticks) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t sdio_slave_recv_frame(lx_frame_t *frame, int32_t timeout_ms)
{
    if (!s_initialized || frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY :
                       pdMS_TO_TICKS(timeout_ms);

    if (xQueueReceive(s_rx_queue, frame, ticks) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t sdio_slave_send_raw(const uint8_t *data, uint16_t len, int32_t timeout_ms)
{
    if (!s_initialized || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY :
                       pdMS_TO_TICKS(timeout_ms);

    /* 对齐到块大小 */
    uint16_t send_len = (len + SDIO_SLAVE_BLOCK_SIZE - 1) &
                        ~(SDIO_SLAVE_BLOCK_SIZE - 1);

    if (send_len > SDIO_SLAVE_BLOCK_SIZE * SDIO_SLAVE_BUF_COUNT) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = sdio_slave_send_packet(data, send_len, ticks);
    if (ret == ESP_OK) {
        xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
        s_stats.tx_bytes += len;
        xSemaphoreGive(s_stats_mutex);
    }

    return ret;
}

esp_err_t sdio_slave_trigger_irq(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 触发中断脉冲 */
    gpio_set_level(SDIO_SLAVE_IRQ_GPIO, 1);
    esp_rom_delay_us(10);  /* 10us 脉冲 */
    gpio_set_level(SDIO_SLAVE_IRQ_GPIO, 0);

    xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
    s_stats.irq_count++;
    xSemaphoreGive(s_stats_mutex);

    return ESP_OK;
}

esp_err_t sdio_slave_get_stats(sdio_slave_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
    memcpy(stats, &s_stats, sizeof(sdio_slave_stats_t));
    xSemaphoreGive(s_stats_mutex);

    return ESP_OK;
}

esp_err_t sdio_slave_clear_stats(void)
{
    xSemaphoreTake(s_stats_mutex, portMAX_DELAY);
    memset(&s_stats, 0, sizeof(s_stats));
    xSemaphoreGive(s_stats_mutex);
    return ESP_OK;
}
