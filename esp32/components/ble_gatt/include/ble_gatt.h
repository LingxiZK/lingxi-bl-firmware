#pragma once
/**
 * @file ble_gatt.h
 * @brief BLE 5.0 GATT 服务组件
 * @version 3.2
 *
 * 支持：
 * - 设备发现广播
 * - 手机APP通信（配置/调试/OTA）
 * - GATT 服务定义
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * 配置常量
 *===========================================================================*/

#define BLE_DEVICE_NAME_MAX_LEN     32
#define BLE_ADV_DATA_MAX_LEN        31
#define BLE_SCAN_RSP_MAX_LEN        31

#define BLE_SVC_UUID                0xFF01   /**< 灵犀主服务 UUID */
#define BLE_CHAR_CONFIG_UUID        0xFF02   /**< 配置特征值 */
#define BLE_CHAR_DEBUG_UUID         0xFF03   /**< 调试特征值 */
#define BLE_CHAR_OTA_UUID           0xFF04   /**< OTA 特征值 */
#define BLE_CHAR_STATUS_UUID        0xFF05   /**< 状态特征值 */

#define BLE_MTU_DEFAULT             23
#define BLE_MTU_MAX                 512

/*=============================================================================
 * 类型定义
 *===========================================================================*/

/**
 * @brief BLE 连接状态
 */
typedef enum {
    BLE_STATE_IDLE = 0,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTED,
} ble_state_t;

/**
 * @brief BLE 事件回调
 */
typedef struct {
    void (*on_connected)(uint16_t conn_handle, void *arg);
    void (*on_disconnected)(uint16_t conn_handle, uint8_t reason, void *arg);
    void (*on_config_rx)(const uint8_t *data, uint16_t len, void *arg);
    void (*on_debug_rx)(const uint8_t *data, uint16_t len, void *arg);
    void (*on_ota_rx)(const uint8_t *data, uint16_t len, void *arg);
    void (*on_mtu_changed)(uint16_t mtu, void *arg);
} ble_gatt_callbacks_t;

/**
 * @brief BLE 配置
 */
typedef struct {
    char     device_name[BLE_DEVICE_NAME_MAX_LEN + 1];
    uint16_t adv_interval_ms;       /**< 广播间隔 (ms) */
    int8_t   tx_power;              /**< 发射功率 (dBm) */
    bool     connectable;           /**< 可连接 */
    bool     scannable;             /**< 可扫描 */
    uint8_t  adv_data[BLE_ADV_DATA_MAX_LEN];
    uint8_t  adv_data_len;
    uint8_t  scan_rsp[BLE_SCAN_RSP_MAX_LEN];
    uint8_t  scan_rsp_len;
} ble_gatt_config_t;

/*=============================================================================
 * API 函数
 *===========================================================================*/

/**
 * @brief 初始化 BLE GATT 服务
 */
esp_err_t ble_gatt_init(const ble_gatt_config_t *config);

/**
 * @brief 反初始化
 */
esp_err_t ble_gatt_deinit(void);

/**
 * @brief 注册事件回调
 */
esp_err_t ble_gatt_register_callbacks(const ble_gatt_callbacks_t *cbs, void *arg);

/**
 * @brief 启动广播
 */
esp_err_t ble_gatt_start_advertising(void);

/**
 * @brief 停止广播
 */
esp_err_t ble_gatt_stop_advertising(void);

/**
 * @brief 发送状态通知
 */
esp_err_t ble_gatt_notify_status(const uint8_t *data, uint16_t len);

/**
 * @brief 发送调试数据通知
 */
esp_err_t ble_gatt_notify_debug(const uint8_t *data, uint16_t len);

/**
 * @brief 发送 OTA 进度通知
 */
esp_err_t ble_gatt_notify_ota_progress(uint8_t progress, uint16_t error_code);

/**
 * @brief 获取当前状态
 */
ble_state_t ble_gatt_get_state(void);

/**
 * @brief 获取连接句柄 (0 = 未连接)
 */
uint16_t ble_gatt_get_conn_handle(void);

/**
 * @brief 断开连接
 */
esp_err_t ble_gatt_disconnect(void);

/**
 * @brief 设置设备名称
 */
esp_err_t ble_gatt_set_device_name(const char *name);

/**
 * @brief 更新连接参数
 */
esp_err_t ble_gatt_update_conn_params(uint16_t min_interval, uint16_t max_interval,
                                      uint16_t latency, uint16_t timeout);

#ifdef __cplusplus
}
#endif
