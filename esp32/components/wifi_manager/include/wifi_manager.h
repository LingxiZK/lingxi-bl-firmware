#pragma once
/**
 * @file wifi_manager.h
 * @brief WiFi 6 (802.11ax) 管理组件
 * @version 3.2
 *
 * 支持 STA 模式（连接无人机飞控 WiFi）和 AP 模式（配网/调试）
 * 图传数据接收（H264 编码流）
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * 配置常量
 *===========================================================================*/

#define WIFI_STA_SSID_MAX_LEN       32
#define WIFI_STA_PASS_MAX_LEN       64
#define WIFI_AP_SSID_MAX_LEN        32
#define WIFI_AP_PASS_MAX_LEN        64
#define WIFI_MAX_STA_CONN           4

#define WIFI_H264_RX_BUF_SIZE       (64 * 1024)   /**< H264 接收缓冲区 64KB */
#define WIFI_H264_RX_PORT           5000          /**< 图传 UDP 端口 */
#define WIFI_CMD_PORT               6000          /**< 控制命令端口 */

/*=============================================================================
 * 类型定义
 *===========================================================================*/

/**
 * @brief WiFi 工作模式
 */
typedef enum {
    WIFI_MODE_NONE = 0,
    WIFI_MODE_STA,          /**< STA 模式（连接飞控） */
    WIFI_MODE_AP,           /**< AP 模式（配网/调试） */
    WIFI_MODE_STA_AP,       /**< STA + AP 共存 */
} wifi_mgr_mode_t;

/**
 * @brief WiFi 连接状态
 */
typedef enum {
    WIFI_CONN_IDLE = 0,
    WIFI_CONN_SCANNING,
    WIFI_CONN_CONNECTING,
    WIFI_CONN_CONNECTED,
    WIFI_CONN_DISCONNECTED,
    WIFI_CONN_FAILED,
} wifi_conn_status_t;

/**
 * @brief WiFi 配置
 */
typedef struct {
    char     sta_ssid[WIFI_STA_SSID_MAX_LEN + 1];
    char     sta_pass[WIFI_STA_PASS_MAX_LEN + 1];
    uint8_t  sta_bssid[6];          /**< 指定 BSSID，全0为自动 */
    int8_t   sta_rssi_threshold;    /**< 最小信号强度，-127为不限制 */
    uint32_t sta_conn_timeout_ms;   /**< 连接超时 */

    char     ap_ssid[WIFI_AP_SSID_MAX_LEN + 1];
    char     ap_pass[WIFI_AP_PASS_MAX_LEN + 1];
    uint8_t  ap_channel;            /**< AP 信道 1-14 */
    uint8_t  ap_max_conn;           /**< 最大连接数 */
    bool     ap_hidden;             /**< 隐藏 SSID */
} wifi_mgr_config_t;

/**
 * @brief WiFi 事件回调
 */
typedef struct {
    void (*on_connected)(const char *ssid, int8_t rssi, void *arg);
    void (*on_disconnected)(uint8_t reason, void *arg);
    void (*on_scan_done)(const wifi_ap_record_t *aps, uint16_t count, void *arg);
    void (*on_sta_connected)(uint8_t mac[6], void *arg);   /**< AP 模式下 STA 接入 */
    void (*on_h264_data)(const uint8_t *data, uint16_t len, void *arg);
    void (*on_cmd_data)(const uint8_t *data, uint16_t len, void *arg);
} wifi_mgr_callbacks_t;

/**
 * @brief WiFi 统计信息
 */
typedef struct {
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t h264_frames;
    uint32_t h264_bytes;
    uint32_t disconnect_count;
    int8_t   rssi;
    uint32_t link_speed;        /**< Mbps */
} wifi_mgr_stats_t;

/*=============================================================================
 * API 函数
 *===========================================================================*/

/**
 * @brief 初始化 WiFi 管理器
 */
esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config);

/**
 * @brief 反初始化
 */
esp_err_t wifi_mgr_deinit(void);

/**
 * @brief 注册事件回调
 */
esp_err_t wifi_mgr_register_callbacks(const wifi_mgr_callbacks_t *cbs, void *arg);

/**
 * @brief 设置工作模式
 */
esp_err_t wifi_mgr_set_mode(wifi_mgr_mode_t mode);

/**
 * @brief 启动 STA 连接
 */
esp_err_t wifi_mgr_sta_connect(void);

/**
 * @brief 断开 STA 连接
 */
esp_err_t wifi_mgr_sta_disconnect(void);

/**
 * @brief 扫描 AP
 */
esp_err_t wifi_mgr_scan(void);

/**
 * @brief 启动 AP
 */
esp_err_t wifi_mgr_ap_start(void);

/**
 * @brief 停止 AP
 */
esp_err_t wifi_mgr_ap_stop(void);

/**
 * @brief 获取当前状态
 */
wifi_conn_status_t wifi_mgr_get_status(void);

/**
 * @brief 获取当前连接信息
 */
esp_err_t wifi_mgr_get_connection_info(char *ssid, int8_t *rssi, uint8_t *bssid);

/**
 * @brief 发送数据到飞控
 */
esp_err_t wifi_mgr_send_cmd(const uint8_t *data, uint16_t len);

/**
 * @brief 获取统计信息
 */
esp_err_t wifi_mgr_get_stats(wifi_mgr_stats_t *stats);

/**
 * @brief 清除统计
 */
esp_err_t wifi_mgr_clear_stats(void);

/**
 * @brief 设置省电模式
 */
esp_err_t wifi_mgr_set_power_save(bool enable);

/**
 * @brief 配置 WiFi 6 特性
 */
esp_err_t wifi_mgr_config_80211ax(bool enable_twt, uint8_t twt_wake_interval);

#ifdef __cplusplus
}
#endif
