#pragma once
/**
 * @file wifi_stream.h
 * @brief Wi-Fi 流转发组件 v4.0
 * @note  接收 STM32N6 的图像帧分片，通过 UDP 广播到地面站
 *        地面站 IP 由配置文件或 DHCP 发现确定
 */

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_STREAM_UDP_PORT     5566   /* 图像数据 UDP 端口 */
#define WIFI_STREAM_MAX_FRAG     256    /* 单帧最大分片数 */
#define WIFI_STREAM_BUF_SIZE     (320 * 240 + 1024)  /* 单帧缓冲 */

/**
 * @brief 初始化 Wi-Fi 流转发
 */
esp_err_t wifi_stream_init(void);

/**
 * @brief 设置地面站目标 IP (可通过 DHCP/广播发现)
 */
esp_err_t wifi_stream_set_target(const char *ip_addr, uint16_t port);

/**
 * @brief 转发一个 SDIO 图像帧分片到地面站
 * @param data  分片数据 (含 app_stream_fragment_header_t)
 * @param len   数据长度
 * @param broadcast 是否广播到所有已知地面站
 */
esp_err_t wifi_stream_forward_fragment(const uint8_t *data, uint16_t len, bool broadcast);

/**
 * @brief 获取流转发统计
 */
typedef struct {
    uint32_t fragments_forwarded;
    uint32_t bytes_forwarded;
    uint32_t frames_completed;
    uint32_t errors;
    uint32_t last_rate_bps;    /* 当前传输速率 bps */
} wifi_stream_stats_t;

esp_err_t wifi_stream_get_stats(wifi_stream_stats_t *stats);

#ifdef __cplusplus
}
#endif
