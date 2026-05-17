/*******************************************************************************
 * @file    main.c
 * @brief   灵犀智空 Lingxi BL — ESP32-C6通信模组入口
 * @version 1.0.0
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_ble_mesh.h"
#include "driver/sdio_slave.h"
#include "lingxi_protocol.h"
#include "wifi_stream.h"

#define TAG "LX_ESP32"

/*============================================================================
 * 任务句柄
 *===========================================================================*/
TaskHandle_t xTaskSDIOHandle;
TaskHandle_t xTaskWiFiHandle;
TaskHandle_t xTaskBLEHandle;
TaskHandle_t xTaskProtocolHandle;

/*============================================================================
 * 事件组
 *===========================================================================*/
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
static EventGroupHandle_t s_wifi_event_group;

/*============================================================================
 * 任务: SDIO Slave通信
 *===========================================================================*/
void vTaskSDIO(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "SDIO task started");

    /* TODO: 初始化SDIO Slave */
    /* sdio_slave_config_t config = { ... }; */
    /* sdio_slave_initialize(&config); */

    for (;;) {
        /* 接收STM32数据 */
        /* 通过lx_process_byte处理字节流 */
        /* 发送响应 */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*============================================================================
 * 任务: WiFi 6管理
 *===========================================================================*/
void vTaskWiFi(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "WiFi task started");

    /* TODO: 初始化WiFi 6 (802.11ax) */
    /* esp_wifi_init(&cfg); */
    /* esp_wifi_set_mode(WIFI_MODE_STA); */

    for (;;) {
        /* 连接无人机飞控WiFi */
        /* 接收图传数据（H264流） */
        /* 转发到上位机 */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*============================================================================
 * 任务: BLE 5.0
 *===========================================================================*/
void vTaskBLE(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "BLE task started");

    /* TODO: 初始化BLE GATT服务 */
    /* 配置服务: 设备信息、配置、OTA、日志 */

    for (;;) {
        /* 处理BLE连接 */
        /* 接收手机APP指令 */
        /* 发送设备状态 */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*============================================================================
 * 任务: 协议处理
 *===========================================================================*/
void vTaskProtocol(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Protocol task started");

    /* 注册协议处理回调 */
    lx_protocol_init();
    lx_register_handler(LX_CMD_SEND_RESULT, on_cmd_result);
    lx_register_handler(LX_CMD_SEND_SENSOR, on_cmd_sensor);
    lx_register_handler(LX_CMD_HEARTBEAT, on_cmd_heartbeat);

    for (;;) {
        /* 处理协议事件 */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*============================================================================
 * 协议回调
 *===========================================================================*/
static void on_cmd_result(const lx_frame_t *frame)
{
    lx_result_t result;
    memcpy(&result, frame->payload, sizeof(result));
    ESP_LOGI(TAG, "Result: %d objects, mode=%d", result.num_objects, result.mode);
    /* 通过WiFi/BLE转发到上位机 */
}

static void on_cmd_sensor(const lx_frame_t *frame)
{
    lx_sensor_t sensor;
    memcpy(&sensor, frame->payload, sizeof(sensor));
    ESP_LOGI(TAG, "ToF=%dmm, UWB=(%d,%d,%d)",
             sensor.tof_distance_mm,
             sensor.uwb_x_mm, sensor.uwb_y_mm, sensor.uwb_z_mm);
}

static void on_cmd_heartbeat(const lx_frame_t *frame)
{
    /* 回复心跳 */
    uint8_t buf[32];
    uint16_t len;
    lx_status_t status = {
        .device_addr = LX_ADDR_ESP32,
        .mode = 0,
        .status = 0,
        .wifi_connected = 1,
        .ble_connected = 0,
        .free_heap = esp_get_free_heap_size(),
    };
    lx_pack_frame(LX_ADDR_ESP32, LX_ADDR_STM32, LX_CMD_HEARTBEAT_ACK,
                   (uint8_t *)&status, sizeof(status), buf, &len);
    /* sdio_slave_send_packet(buf, len); */
}

/*============================================================================
 * WiFi事件处理
 *===========================================================================*/
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/*============================================================================
 * 主函数
 *===========================================================================*/
void app_main(void)
{
    ESP_LOGI(TAG, "=== Lingxi BL ESP32-C6 Boot ===");

    /* 初始化NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 初始化TCP/IP栈 */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 初始化WiFi */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    s_wifi_event_group = xEventGroupCreate();

    /* 初始化 Wi-Fi 流转发 (图像帧 → UDP 地面站) */
    ESP_ERROR_CHECK(wifi_stream_init());
    /* TODO: 通过配置或 DHCP 发现获取地面站 IP */
    wifi_stream_set_target("192.168.1.100", WIFI_STREAM_UDP_PORT);

    /* 创建任务 */
    xTaskCreate(vTaskSDIO, "SDIO", 4096, NULL, 4, &xTaskSDIOHandle);
    xTaskCreate(vTaskWiFi, "WiFi", 8192, NULL, 3, &xTaskWiFiHandle);
    xTaskCreate(vTaskBLE, "BLE", 4096, NULL, 2, &xTaskBLEHandle);
    xTaskCreate(vTaskProtocol, "Protocol", 4096, NULL, 3, &xTaskProtocolHandle);

    ESP_LOGI(TAG, "All tasks created");
}
