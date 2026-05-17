#include "ble_gatt.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "BLE_GATT"

/*=============================================================================
 * GATT 服务定义
 *===========================================================================*/

/* 主服务 UUID: 0xFF01 */
static const uint16_t s_primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t s_character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t s_character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t s_service_uuid[16] = {
    /* 128-bit UUID: 0000FF01-0000-1000-8000-00805F9B34FB */
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00
};

/* 特征值 UUID */
static const uint8_t s_char_config_uuid[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00
};

static const uint8_t s_char_debug_uuid[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x03, 0xFF, 0x00, 0x00
};

static const uint8_t s_char_ota_uuid[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x04, 0xFF, 0x00, 0x00
};

static const uint8_t s_char_status_uuid[16] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x05, 0xFF, 0x00, 0x00
};

/* 特征值属性 */
#define CHAR_PROP_READ          ESP_GATT_CHAR_PROP_BIT_READ
#define CHAR_PROP_WRITE         ESP_GATT_CHAR_PROP_BIT_WRITE
#define CHAR_PROP_NOTIFY        ESP_GATT_CHAR_PROP_BIT_NOTIFY
#define CHAR_PROP_WRITE_NR      ESP_GATT_CHAR_PROP_BIT_WRITE_NR

/* 属性权限 */
#define ATTR_PERM_READ          ESP_GATT_PERM_READ
#define ATTR_PERM_WRITE         ESP_GATT_PERM_WRITE

/*=============================================================================
 * 内部状态
 *===========================================================================*/

static ble_gatt_config_t    s_config = {0};
static ble_gatt_callbacks_t s_cbs = {0};
static void                *s_cb_arg = NULL;
static ble_state_t          s_state = BLE_STATE_IDLE;
static uint16_t             s_conn_handle = 0;
static uint16_t             s_mtu = BLE_MTU_DEFAULT;
static SemaphoreHandle_t    s_mutex = NULL;
static bool                 s_initialized = false;

/* GATT 服务句柄 */
static uint16_t s_gatts_if = 0;
static uint16_t s_service_handle = 0;
static uint16_t s_char_config_handle = 0;
static uint16_t s_char_debug_handle = 0;
static uint16_t s_char_ota_handle = 0;
static uint16_t s_char_status_handle = 0;

/* CCCD (Client Characteristic Configuration Descriptor) 句柄 */
static uint16_t s_cccd_status_handle = 0;
static uint16_t s_cccd_debug_handle = 0;
static uint16_t s_cccd_ota_handle = 0;

/*=============================================================================
 * GATT 属性数据库
 *===========================================================================*/

/* 特征值声明 */
static const uint8_t s_char_config_prop = CHAR_PROP_WRITE | CHAR_PROP_WRITE_NR;
static const uint8_t s_char_debug_prop  = CHAR_PROP_WRITE | CHAR_PROP_NOTIFY;
static const uint8_t s_char_ota_prop    = CHAR_PROP_WRITE | CHAR_PROP_NOTIFY;
static const uint8_t s_char_status_prop = CHAR_PROP_READ | CHAR_PROP_NOTIFY;

/* 属性表 */
static const esp_gatts_attr_db_t s_gatt_db[] = {
    /* 服务声明 */
    [0] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&s_primary_service_uuid,
           ESP_GATT_PERM_READ, sizeof(s_service_uuid), sizeof(s_service_uuid),
           (uint8_t *)s_service_uuid}},

    /* 配置特征值声明 */
    [1] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&s_character_declaration_uuid,
           ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&s_char_config_prop}},
    /* 配置特征值 */
    [2] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)s_char_config_uuid,
           ESP_GATT_PERM_WRITE, 256, 0, NULL}},

    /* 调试特征值声明 */
    [3] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&s_character_declaration_uuid,
           ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&s_char_debug_prop}},
    /* 调试特征值 */
    [4] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)s_char_debug_uuid,
           ESP_GATT_PERM_WRITE, 256, 0, NULL}},
    /* 调试 CCCD */
    [5] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&s_character_client_config_uuid,
           ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 0, NULL}},

    /* OTA 特征值声明 */
    [6] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&s_character_declaration_uuid,
           ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&s_char_ota_prop}},
    /* OTA 特征值 */
    [7] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)s_char_ota_uuid,
           ESP_GATT_PERM_WRITE, 512, 0, NULL}},
    /* OTA CCCD */
    [8] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&s_character_client_config_uuid,
           ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 0, NULL}},

    /* 状态特征值声明 */
    [9] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&s_character_declaration_uuid,
           ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&s_char_status_prop}},
    /* 状态特征值 */
    [10] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)s_char_status_uuid,
            ESP_GATT_PERM_READ, 128, 0, NULL}},
    /* 状态 CCCD */
    [11] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&s_character_client_config_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 0, NULL}},
};

#define GATT_DB_SIZE    (sizeof(s_gatt_db) / sizeof(s_gatt_db[0]))

/*=============================================================================
 * GAP 回调
 *===========================================================================*/

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising started");
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_state = BLE_STATE_ADVERTISING;
                xSemaphoreGive(s_mutex);
            } else {
                ESP_LOGE(TAG, "Advertising start failed: %d", param->adv_start_cmpl.status);
            }
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising stopped");
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_state = BLE_STATE_IDLE;
            xSemaphoreGive(s_mutex);
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(TAG, "Connection params updated: int=%d, lat=%d, timeout=%d",
                     param->update_conn_params.conn_int,
                     param->update_conn_params.latency,
                     param->update_conn_params.timeout);
            break;

        default:
            break;
    }
}

/*=============================================================================
 * GATT Server 回调
 *===========================================================================*/

static void gatts_event_handler(esp_gatts_cb_event_t event,
                              esp_gatt_if_t gatts_if,
                              esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                s_gatts_if = gatts_if;
                ESP_LOGI(TAG, "GATT server registered, app_id=%d", param->reg.app_id);

                /* 创建服务 */
                esp_ble_gatts_create_attr_tab(s_gatt_db, gatts_if, GATT_DB_SIZE, 0);
            } else {
                ESP_LOGE(TAG, "GATT server registration failed: %d", param->reg.status);
            }
            break;

        case ESP_GATTS_CREATE_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK) {
                s_service_handle = param->add_attr_tab.handles[0];
                s_char_config_handle = param->add_attr_tab.handles[2];
                s_char_debug_handle = param->add_attr_tab.handles[4];
                s_cccd_debug_handle = param->add_attr_tab.handles[5];
                s_char_ota_handle = param->add_attr_tab.handles[7];
                s_cccd_ota_handle = param->add_attr_tab.handles[8];
                s_char_status_handle = param->add_attr_tab.handles[10];
                s_cccd_status_handle = param->add_attr_tab.handles[11];

                ESP_LOGI(TAG, "Attribute table created, service_handle=%d", s_service_handle);

                /* 启动服务 */
                esp_ble_gatts_start_service(s_service_handle);
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_conn_handle = param->connect.conn_id;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_state = BLE_STATE_CONNECTED;
            xSemaphoreGive(s_mutex);

            ESP_LOGI(TAG, "Connected, conn_handle=%d, addr="MACSTR,
                     s_conn_handle, MAC2STR(param->connect.remote_bda));

            /* 更新连接参数 */
            esp_ble_gap_update_conn_params(&param->connect);

            if (s_cbs.on_connected) {
                s_cbs.on_connected(s_conn_handle, s_cb_arg);
            }
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Disconnected, reason=%d", param->disconnect.reason);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_state = BLE_STATE_DISCONNECTED;
            s_conn_handle = 0;
            xSemaphoreGive(s_mutex);

            if (s_cbs.on_disconnected) {
                s_cbs.on_disconnected(s_conn_handle, param->disconnect.reason, s_cb_arg);
            }

            /* 重新启动广播 */
            ble_gatt_start_advertising();
            break;

        case ESP_GATTS_WRITE_EVT: {
            uint16_t handle = param->write.handle;
            uint8_t *data = param->write.value;
            uint16_t len = param->write.len;

            if (handle == s_char_config_handle) {
                ESP_LOGI(TAG, "Config write, len=%d", len);
                if (s_cbs.on_config_rx) {
                    s_cbs.on_config_rx(data, len, s_cb_arg);
                }
            } else if (handle == s_char_debug_handle) {
                ESP_LOGI(TAG, "Debug write, len=%d", len);
                if (s_cbs.on_debug_rx) {
                    s_cbs.on_debug_rx(data, len, s_cb_arg);
                }
            } else if (handle == s_char_ota_handle) {
                ESP_LOGI(TAG, "OTA write, len=%d", len);
                if (s_cbs.on_ota_rx) {
                    s_cbs.on_ota_rx(data, len, s_cb_arg);
                }
            } else if (handle == s_cccd_status_handle ||
                       handle == s_cccd_debug_handle ||
                       handle == s_cccd_ota_handle) {
                /* CCCD 写入，记录通知使能状态 */
                ESP_LOGI(TAG, "CCCD write, handle=%d, value=0x%04X", handle,
                         (data[1] << 8) | data[0]);
            }

            /* 发送写响应 */
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                              param->write.trans_id,
                                              ESP_GATT_OK, NULL);
            }
            break;
        }

        case ESP_GATTS_READ_EVT: {
            /* 发送读响应 */
            esp_gatt_rsp_t rsp = {0};
            rsp.attr_value.handle = param->read.handle;

            if (param->read.handle == s_char_status_handle) {
                /* 返回状态数据 */
                const char *status = "{"status":"ok"}";
                rsp.attr_value.len = strlen(status);
                memcpy(rsp.attr_value.value, status, rsp.attr_value.len);
            }

            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                        param->read.trans_id,
                                        ESP_GATT_OK, &rsp);
            break;
        }

        case ESP_GATTS_MTU_EVT:
            s_mtu = param->mtu.mtu;
            ESP_LOGI(TAG, "MTU updated: %d", s_mtu);
            if (s_cbs.on_mtu_changed) {
                s_cbs.on_mtu_changed(s_mtu, s_cb_arg);
            }
            break;

        default:
            break;
    }
}

/*=============================================================================
 * API 实现
 *===========================================================================*/

esp_err_t ble_gatt_init(const ble_gatt_config_t *config)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(ble_gatt_config_t));

    /* 创建互斥锁 */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* 初始化蓝牙控制器 */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %d", ret);
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %d", ret);
        esp_bt_controller_deinit();
        return ret;
    }

    /* 初始化 Bluedroid */
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %d", ret);
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %d", ret);
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return ret;
    }

    /* 注册 GAP 回调 */
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback registration failed: %d", ret);
        goto fail;
    }

    /* 注册 GATT 回调 */
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT callback registration failed: %d", ret);
        goto fail;
    }

    /* 注册 GATT 应用 */
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT app registration failed: %d", ret);
        goto fail;
    }

    /* 设置设备名称 */
    esp_ble_gap_set_device_name(s_config.device_name);

    /* 配置广播数据 */
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = s_config.adv_interval_ms / 0.625,
        .max_interval = s_config.adv_interval_ms / 0.625,
        .appearance = 0,
        .manufacturer_len = s_config.adv_data_len,
        .p_manufacturer_data = s_config.adv_data,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(s_service_uuid),
        .p_service_uuid = (uint8_t *)s_service_uuid,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };

    esp_ble_gap_config_adv_data(&adv_data);

    /* 配置扫描响应 */
    if (s_config.scan_rsp_len > 0) {
        esp_ble_adv_data_t scan_rsp = {
            .set_scan_rsp = true,
            .include_name = true,
            .manufacturer_len = s_config.scan_rsp_len,
            .p_manufacturer_data = s_config.scan_rsp,
        };
        esp_ble_gap_config_adv_data(&scan_rsp);
    }

    /* 设置发射功率 */
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, s_config.tx_power);

    s_initialized = true;
    ESP_LOGI(TAG, "BLE GATT initialized, name=%s", s_config.device_name);
    return ESP_OK;

fail:
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    return ret;
}

esp_err_t ble_gatt_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_gatt_stop_advertising();

    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_initialized = false;
    s_conn_handle = 0;
    s_state = BLE_STATE_IDLE;

    ESP_LOGI(TAG, "BLE GATT deinitialized");
    return ESP_OK;
}

esp_err_t ble_gatt_register_callbacks(const ble_gatt_callbacks_t *cbs, void *arg)
{
    if (cbs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_cbs, cbs, sizeof(ble_gatt_callbacks_t));
    s_cb_arg = arg;
    return ESP_OK;
}

esp_err_t ble_gatt_start_advertising(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_ble_adv_params_t adv_params = {
        .adv_int_min = s_config.adv_interval_ms / 0.625,
        .adv_int_max = s_config.adv_interval_ms / 0.625,
        .adv_type = s_config.connectable ? ADV_TYPE_IND : ADV_TYPE_NONCONN_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .peer_addr = {0},
        .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };

    esp_err_t ret = esp_ble_gap_start_advertising(&adv_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start advertising failed: %d", ret);
        return ret;
    }

    return ESP_OK;
}

esp_err_t ble_gatt_stop_advertising(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_ble_gap_stop_advertising();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop advertising failed: %d", ret);
        return ret;
    }

    return ESP_OK;
}

esp_err_t ble_gatt_notify_status(const uint8_t *data, uint16_t len)
{
    if (!s_initialized || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_conn_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_ble_gatts_send_indicate(s_gatts_if, s_conn_handle,
                                       s_char_status_handle, len,
                                       (uint8_t *)data, false);
}

esp_err_t ble_gatt_notify_debug(const uint8_t *data, uint16_t len)
{
    if (!s_initialized || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_conn_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_ble_gatts_send_indicate(s_gatts_if, s_conn_handle,
                                       s_char_debug_handle, len,
                                       (uint8_t *)data, false);
}

esp_err_t ble_gatt_notify_ota_progress(uint8_t progress, uint16_t error_code)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_conn_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[3] = {progress, (uint8_t)(error_code >> 8), (uint8_t)(error_code & 0xFF)};

    return esp_ble_gatts_send_indicate(s_gatts_if, s_conn_handle,
                                       s_char_ota_handle, sizeof(data),
                                       data, false);
}

ble_state_t ble_gatt_get_state(void)
{
    ble_state_t state;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    state = s_state;
    xSemaphoreGive(s_mutex);
    return state;
}

uint16_t ble_gatt_get_conn_handle(void)
{
    return s_conn_handle;
}

esp_err_t ble_gatt_disconnect(void)
{
    if (!s_initialized || s_conn_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_ble_gatts_close(s_gatts_if, s_conn_handle);
}

esp_err_t ble_gatt_set_device_name(const char *name)
{
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_config.device_name, name, BLE_DEVICE_NAME_MAX_LEN);
    s_config.device_name[BLE_DEVICE_NAME_MAX_LEN] = '\0';

    if (s_initialized) {
        return esp_ble_gap_set_device_name(s_config.device_name);
    }

    return ESP_OK;
}

esp_err_t ble_gatt_update_conn_params(uint16_t min_interval, uint16_t max_interval,
                                      uint16_t latency, uint16_t timeout)
{
    if (!s_initialized || s_conn_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_ble_conn_update_params_t params = {
        .min_int = min_interval,
        .max_int = max_interval,
        .latency = latency,
        .timeout = timeout,
    };

    return esp_ble_gap_update_conn_params(&params);
}
