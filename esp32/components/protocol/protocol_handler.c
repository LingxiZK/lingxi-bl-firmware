#include "protocol_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>

#define TAG "PROTO_HANDLER"

/*=============================================================================
 * 内部状态
 *===========================================================================*/

typedef struct {
    proto_cmd_handler_t handler;
    void *arg;
} cmd_handler_entry_t;

static cmd_handler_entry_t s_cmd_handlers[LX_FRAME_TYPE_MAX + 1] = {0};
static proto_event_cb_t    s_event_cb = NULL;
static void               *s_event_arg = NULL;
static SemaphoreHandle_t     s_mutex = NULL;
static bool                  s_initialized = false;

/* 发送队列 */
static QueueHandle_t s_tx_queue = NULL;

/*=============================================================================
 * 内部函数
 *===========================================================================*/

static esp_err_t proto_tx_task(void *arg)
{
    (void)arg;
    lx_frame_t frame;

    while (1) {
        if (xQueueReceive(s_tx_queue, &frame, portMAX_DELAY) == pdPASS) {
            /* 通过 SDIO 发送 */
            extern esp_err_t sdio_slave_send_frame(const lx_frame_t *frame, int32_t timeout_ms);
            esp_err_t ret = sdio_slave_send_frame(&frame, 1000);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send frame: %d", ret);
            }
        }
    }

    return ESP_OK;
}

static esp_err_t handle_ping(const lx_frame_t *cmd, lx_frame_t *resp, void *arg)
{
    (void)cmd;
    (void)arg;

    /* 发送 PONG */
    lx_frame_build(resp, LX_CMD_PONG, LX_FLAG_REQ_ACK, NULL, 0);
    return ESP_OK;
}

static esp_err_t handle_get_status(const lx_frame_t *cmd, lx_frame_t *resp, void *arg)
{
    (void)cmd;
    (void)arg;

    lx_payload_status_t status = {0};
    status.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    status.esp_status = LX_ESP_STATUS_OK | LX_ESP_STATUS_SDIO_RDY;
    status.wifi_status = LX_WIFI_OFF;
    status.ble_status = LX_BLE_OFF;
    status.sdio_status = 0;
    status.free_heap = esp_get_free_heap_size();

    lx_frame_build(resp, LX_DATA_STATUS, LX_FLAG_REQ_ACK,
                   (const uint8_t *)&status, sizeof(status));
    return ESP_OK;
}

static esp_err_t handle_set_mode(const lx_frame_t *cmd, lx_frame_t *resp, void *arg)
{
    (void)arg;

    if (cmd->payload_len < 1) {
        lx_frame_build(resp, LX_ACK_ERR_INVALID, 0, NULL, 0);
        return ESP_OK;
    }

    uint8_t mode = cmd->payload[0];
    ESP_LOGI(TAG, "Set mode: %d", mode);

    lx_frame_build(resp, LX_ACK_OK, 0, NULL, 0);
    return ESP_OK;
}

static esp_err_t handle_reset_esp(const lx_frame_t *cmd, lx_frame_t *resp, void *arg)
{
    (void)cmd;
    (void)arg;

    /* 发送应答 */
    lx_frame_build(resp, LX_ACK_OK, 0, NULL, 0);

    /* 延迟复位 */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
}

/*=============================================================================
 * API 实现
 *===========================================================================*/

esp_err_t proto_handler_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_tx_queue = xQueueCreate(16, sizeof(lx_frame_t));
    if (s_tx_queue == NULL) {
        vSemaphoreDelete(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* 创建发送任务 */
    xTaskCreate((TaskFunction_t)proto_tx_task, "proto_tx", 4096, NULL, 8, NULL);

    /* 注册默认命令处理器 */
    proto_handler_register_cmd(LX_CMD_PING, handle_ping, NULL);
    proto_handler_register_cmd(LX_CMD_GET_STATUS, handle_get_status, NULL);
    proto_handler_register_cmd(LX_CMD_SET_MODE, handle_set_mode, NULL);
    proto_handler_register_cmd(LX_CMD_RESET_ESP, handle_reset_esp, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "Protocol handler initialized");
    return ESP_OK;
}

esp_err_t proto_handler_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_cmd_handlers, 0, sizeof(s_cmd_handlers));
    s_event_cb = NULL;

    if (s_tx_queue) {
        vQueueDelete(s_tx_queue);
        s_tx_queue = NULL;
    }
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_initialized = false;
    return ESP_OK;
}

esp_err_t proto_handler_register_cmd(lx_frame_type_t type, proto_cmd_handler_t handler, void *arg)
{
    if (type > LX_FRAME_TYPE_MAX || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_cmd_handlers[type].handler = handler;
    s_cmd_handlers[type].arg = arg;
    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t proto_handler_unregister_cmd(lx_frame_type_t type)
{
    if (type > LX_FRAME_TYPE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_cmd_handlers[type].handler = NULL;
    s_cmd_handlers[type].arg = NULL;
    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t proto_handler_register_event(proto_event_cb_t cb, void *arg)
{
    s_event_cb = cb;
    s_event_arg = arg;
    return ESP_OK;
}

esp_err_t proto_handler_process_frame(const lx_frame_t *frame)
{
    if (!s_initialized || frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 验证帧 */
    if (!lx_frame_validate(frame)) {
        ESP_LOGW(TAG, "Frame validation failed");
        return ESP_ERR_INVALID_CRC;
    }

    uint8_t type = frame->header.type;
    ESP_LOGI(TAG, "Process frame type=0x%02X", type);

    /* 命令帧处理 */
    if (type >= LX_CMD_PING && type <= 0x1F) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        cmd_handler_entry_t *entry = &s_cmd_handlers[type];
        xSemaphoreGive(s_mutex);

        if (entry->handler) {
            lx_frame_t resp = {0};
            esp_err_t ret = entry->handler(frame, &resp, entry->arg);
            if (ret == ESP_OK && frame->header.flags & LX_FLAG_REQ_ACK) {
                /* 发送应答 */
                if (xQueueSend(s_tx_queue, &resp, pdMS_TO_TICKS(100)) != pdPASS) {
                    ESP_LOGW(TAG, "TX queue full");
                }
            }
        } else {
            /* 未注册的命令，发送错误应答 */
            lx_frame_t resp = {0};
            lx_frame_build(&resp, LX_ACK_ERR_TYPE, 0, NULL, 0);
            if (xQueueSend(s_tx_queue, &resp, pdMS_TO_TICKS(100)) != pdPASS) {
                ESP_LOGW(TAG, "TX queue full");
            }
        }
    }
    /* 数据帧处理 */
    else if (type >= 0x20 && type <= 0x4F) {
        if (s_event_cb) {
            s_event_cb(type, frame->payload, s_event_arg);
        }
    }
    /* 事件帧处理 */
    else if (type >= 0x50 && type <= 0x6F) {
        if (s_event_cb) {
            s_event_cb(type, frame->payload, s_event_arg);
        }
    }
    /* 应答帧 */
    else if (type >= 0x70 && type <= 0x7F) {
        ESP_LOGI(TAG, "ACK received: type=0x%02X", type);
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_status(const lx_payload_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, LX_DATA_STATUS, 0,
                             (const uint8_t *)status, sizeof(lx_payload_status_t));
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_obstacle(const lx_payload_obstacle_t *obstacle)
{
    if (obstacle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, LX_DATA_OBSTACLE, LX_FLAG_PRIORITY,
                             (const uint8_t *)obstacle, sizeof(lx_payload_obstacle_t));
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_tof(const lx_payload_tof_t *tof)
{
    if (tof == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, LX_DATA_TOF, 0,
                             (const uint8_t *)tof, sizeof(lx_payload_tof_t));
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_uwb(const lx_payload_uwb_t *uwb)
{
    if (uwb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, LX_DATA_UWB, 0,
                             (const uint8_t *)uwb, sizeof(lx_payload_uwb_t));
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_log(const char *log, uint16_t len)
{
    if (log == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > LX_PAYLOAD_MAX_LEN) {
        len = LX_PAYLOAD_MAX_LEN;
    }

    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, LX_DATA_LOG, 0, (const uint8_t *)log, len);
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_event(lx_frame_type_t event_type, const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || event_type < 0x50 || event_type > 0x6F) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > LX_PAYLOAD_MAX_LEN) {
        len = LX_PAYLOAD_MAX_LEN;
    }

    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, event_type, 0, data, len);
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_ack(lx_frame_type_t ack_type, const uint8_t *data, uint16_t len)
{
    if (ack_type < 0x70 || ack_type > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > LX_PAYLOAD_MAX_LEN) {
        len = LX_PAYLOAD_MAX_LEN;
    }

    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, ack_type, 0, data, len);
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_ping(void)
{
    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, LX_CMD_PING, LX_FLAG_REQ_ACK, NULL, 0);
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t proto_handler_send_pong(void)
{
    lx_frame_t frame = {0};
    int ret = lx_frame_build(&frame, LX_CMD_PONG, 0, NULL, 0);
    if (ret != 0) {
        return ESP_FAIL;
    }

    if (xQueueSend(s_tx_queue, &frame, pdMS_TO_TICKS(100)) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
