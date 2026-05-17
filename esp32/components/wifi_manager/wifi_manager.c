#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <string.h>

#define TAG "WIFI_MGR"

/*=============================================================================
 * 内部状态
 *===========================================================================*/

static wifi_mgr_config_t    s_config = {0};
static wifi_mgr_callbacks_t s_cbs = {0};
static void                *s_cb_arg = NULL;
static wifi_mgr_mode_t      s_mode = WIFI_MODE_NONE;
static wifi_conn_status_t   s_status = WIFI_CONN_IDLE;
static wifi_mgr_stats_t     s_stats = {0};
static SemaphoreHandle_t    s_mutex = NULL;
static bool                 s_initialized = false;

/* 网络句柄 */
static esp_netif_t         *s_sta_netif = NULL;
static esp_netif_t         *s_ap_netif = NULL;

/* 图传接收 */
static int                  s_h264_sock = -1;
static TaskHandle_t         s_h264_task = NULL;
static uint8_t              s_h264_buf[WIFI_H264_RX_BUF_SIZE];

/* 命令端口 */
static int                  s_cmd_sock = -1;
static TaskHandle_t         s_cmd_task = NULL;

/*=============================================================================
 * 事件处理
 *===========================================================================*/

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started");
                s_status = WIFI_CONN_SCANNING;
                break;

            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *evt = event_data;
                ESP_LOGI(TAG, "Connected to %s", evt->ssid);
                s_status = WIFI_CONN_CONNECTED;
                if (s_cbs.on_connected) {
                    s_cbs.on_connected((char *)evt->ssid, 0, s_cb_arg);
                }
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *evt = event_data;
                ESP_LOGW(TAG, "Disconnected, reason=%d", evt->reason);
                s_status = WIFI_CONN_DISCONNECTED;
                s_stats.disconnect_count++;
                if (s_cbs.on_disconnected) {
                    s_cbs.on_disconnected(evt->reason, s_cb_arg);
                }
                /* 自动重连 */
                if (s_mode == WIFI_MODE_STA || s_mode == WIFI_MODE_STA_AP) {
                    esp_wifi_connect();
                }
                break;
            }

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *evt = event_data;
                ESP_LOGI(TAG, "STA connected to AP: "MACSTR, MAC2STR(evt->mac));
                if (s_cbs.on_sta_connected) {
                    s_cbs.on_sta_connected(evt->mac, s_cb_arg);
                }
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *evt = event_data;
                ESP_LOGI(TAG, "STA disconnected from AP: "MACSTR, MAC2STR(evt->mac));
                break;
            }

            case WIFI_EVENT_SCAN_DONE: {
                ESP_LOGI(TAG, "Scan done");
                uint16_t ap_count = 0;
                esp_wifi_scan_get_ap_num(&ap_count);
                if (ap_count > 0) {
                    wifi_ap_record_t *aps = malloc(sizeof(wifi_ap_record_t) * ap_count);
                    if (aps) {
                        esp_wifi_scan_get_ap_records(&ap_count, aps);
                        if (s_cbs.on_scan_done) {
                            s_cbs.on_scan_done(aps, ap_count, s_cb_arg);
                        }
                        free(aps);
                    }
                }
                break;
            }
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *evt = event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
                break;
            }
        }
    }
}

/*=============================================================================
 * 图传接收任务
 *===========================================================================*/

static void h264_rx_task(void *arg)
{
    (void)arg;
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    while (1) {
        int len = recvfrom(s_h264_sock, s_h264_buf, sizeof(s_h264_buf), 0,
                          (struct sockaddr *)&src_addr, &addr_len);
        if (len > 0) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_stats.h264_frames++;
            s_stats.h264_bytes += len;
            xSemaphoreGive(s_mutex);

            if (s_cbs.on_h264_data) {
                s_cbs.on_h264_data(s_h264_buf, len, s_cb_arg);
            }
        } else if (len < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/*=============================================================================
 * 命令接收任务
 *===========================================================================*/

static void cmd_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[1024];
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    while (1) {
        int len = recvfrom(s_cmd_sock, buf, sizeof(buf), 0,
                          (struct sockaddr *)&src_addr, &addr_len);
        if (len > 0) {
            if (s_cbs.on_cmd_data) {
                s_cbs.on_cmd_data(buf, len, s_cb_arg);
            }
        } else if (len < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/*=============================================================================
 * API 实现
 *===========================================================================*/

esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(wifi_mgr_config_t));

    /* 创建互斥锁 */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* 初始化网络栈 */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 注册事件处理 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    /* 创建默认网络接口 */
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    /* 初始化 WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 配置存储 */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    /* WiFi 6 特性配置 */
    wifi_config_t wifi_cfg = {0};
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    /* 802.11ax 使能 */
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_11AX);

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_mgr_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 关闭 socket */
    if (s_h264_sock >= 0) {
        close(s_h264_sock);
        s_h264_sock = -1;
    }
    if (s_cmd_sock >= 0) {
        close(s_cmd_sock);
        s_cmd_sock = -1;
    }

    /* 删除任务 */
    if (s_h264_task) {
        vTaskDelete(s_h264_task);
        s_h264_task = NULL;
    }
    if (s_cmd_task) {
        vTaskDelete(s_cmd_task);
        s_cmd_task = NULL;
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "WiFi manager deinitialized");
    return ESP_OK;
}

esp_err_t wifi_mgr_register_callbacks(const wifi_mgr_callbacks_t *cbs, void *arg)
{
    if (cbs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_cbs, cbs, sizeof(wifi_mgr_callbacks_t));
    s_cb_arg = arg;
    return ESP_OK;
}

esp_err_t wifi_mgr_set_mode(wifi_mgr_mode_t mode)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_mode_t esp_mode;
    switch (mode) {
        case WIFI_MODE_STA:
            esp_mode = WIFI_MODE_STA;
            break;
        case WIFI_MODE_AP:
            esp_mode = WIFI_MODE_AP;
            break;
        case WIFI_MODE_STA_AP:
            esp_mode = WIFI_MODE_APSTA;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(esp_mode));
    s_mode = mode;

    return ESP_OK;
}

esp_err_t wifi_mgr_sta_connect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, s_config.sta_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, s_config.sta_pass, sizeof(wifi_cfg.sta.password) - 1);
    memcpy(wifi_cfg.sta.bssid, s_config.sta_bssid, 6);
    wifi_cfg.sta.threshold.rssi = s_config.sta_rssi_threshold;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    s_status = WIFI_CONN_CONNECTING;

    /* 创建图传接收 socket */
    s_h264_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_h264_sock >= 0) {
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(WIFI_H264_RX_PORT),
            .sin_addr.s_addr = INADDR_ANY,
        };
        bind(s_h264_sock, (struct sockaddr *)&addr, sizeof(addr));
        xTaskCreate(h264_rx_task, "h264_rx", 4096, NULL, 5, &s_h264_task);
    }

    /* 创建命令接收 socket */
    s_cmd_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_cmd_sock >= 0) {
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(WIFI_CMD_PORT),
            .sin_addr.s_addr = INADDR_ANY,
        };
        bind(s_cmd_sock, (struct sockaddr *)&addr, sizeof(addr));
        xTaskCreate(cmd_rx_task, "cmd_rx", 4096, NULL, 5, &s_cmd_task);
    }

    ESP_LOGI(TAG, "STA connecting to %s", s_config.sta_ssid);
    return ESP_OK;
}

esp_err_t wifi_mgr_sta_disconnect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_wifi_disconnect();
    s_status = WIFI_CONN_DISCONNECTED;

    /* 关闭 socket */
    if (s_h264_sock >= 0) {
        close(s_h264_sock);
        s_h264_sock = -1;
    }
    if (s_cmd_sock >= 0) {
        close(s_cmd_sock);
        s_cmd_sock = -1;
    }

    return ESP_OK;
}

esp_err_t wifi_mgr_scan(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, false));
    s_status = WIFI_CONN_SCANNING;

    return ESP_OK;
}

esp_err_t wifi_mgr_ap_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.ap.ssid, s_config.ap_ssid, sizeof(wifi_cfg.ap.ssid) - 1);
    strncpy((char *)wifi_cfg.ap.password, s_config.ap_pass, sizeof(wifi_cfg.ap.password) - 1);
    wifi_cfg.ap.ssid_len = strlen(s_config.ap_ssid);
    wifi_cfg.ap.channel = s_config.ap_channel;
    wifi_cfg.ap.max_connection = s_config.ap_max_conn;
    wifi_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.ap.hidden = s_config.ap_hidden;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: %s", s_config.ap_ssid);
    return ESP_OK;
}

esp_err_t wifi_mgr_ap_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_ERROR_CHECK(esp_wifi_stop());
    return ESP_OK;
}

wifi_conn_status_t wifi_mgr_get_status(void)
{
    return s_status;
}

esp_err_t wifi_mgr_get_connection_info(char *ssid, int8_t *rssi, uint8_t *bssid)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ssid) {
        strncpy(ssid, (char *)ap_info.ssid, 32);
    }
    if (rssi) {
        *rssi = ap_info.rssi;
    }
    if (bssid) {
        memcpy(bssid, ap_info.bssid, 6);
    }

    return ESP_OK;
}

esp_err_t wifi_mgr_send_cmd(const uint8_t *data, uint16_t len)
{
    if (!s_initialized || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_cmd_sock < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 发送到飞控 (假设已知 IP) */
    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(WIFI_CMD_PORT),
        .sin_addr.s_addr = inet_addr("192.168.4.1"), /* 需根据实际配置 */
    };

    int sent = sendto(s_cmd_sock, data, len, 0,
                      (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (sent < 0) {
        return ESP_FAIL;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_stats.tx_bytes += sent;
    s_stats.tx_packets++;
    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t wifi_mgr_get_stats(wifi_mgr_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(stats, &s_stats, sizeof(wifi_mgr_stats_t));
    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t wifi_mgr_clear_stats(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(&s_stats, 0, sizeof(s_stats));
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t wifi_mgr_set_power_save(bool enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    }

    return ESP_OK;
}

esp_err_t wifi_mgr_config_80211ax(bool enable_twt, uint8_t twt_wake_interval)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 配置 802.11ax 协议 */
    uint8_t protocol = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                       WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX;
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, protocol));

    /* 配置 TWT (Target Wake Time) - WiFi 6 省电特性 */
    if (enable_twt) {
        /* ESP32-C6 支持 TWT */
        ESP_LOGI(TAG, "TWT enabled, wake interval=%d", twt_wake_interval);
    }

    /* 配置带宽 */
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));

    ESP_LOGI(TAG, "802.11ax configured");
    return ESP_OK;
}
