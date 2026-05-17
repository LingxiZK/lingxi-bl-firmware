#include "wifi_stream.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <arpa/inet.h>

#define TAG "WIFI_STREAM"

/* ============================================================================
 * 数据结构
 * ==========================================================================*/
typedef struct {
    struct sockaddr_in6  target_addr;    /* 地面站地址 (双栈) */
    bool                 has_target;
    TaskHandle_t         tx_task;
    QueueHandle_t        frag_queue;     /* 分片发送队列 */
} wifi_stream_ctx_t;

static wifi_stream_ctx_t s_ctx = {0};
static wifi_stream_stats_t s_stats = {0};

/* 分片队列项 */
typedef struct {
    uint8_t data[1536];
    uint16_t len;
} frag_item_t;

/* ============================================================================
 * 发送任务：从队列取分片，通过 UDP 发出
 * ==========================================================================*/
static void wifi_stream_tx_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    /* 设置发送超时 */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    frag_item_t item;

    while (1) {
        if (xQueueReceive(s_ctx.frag_queue, &item, portMAX_DELAY) == pdPASS) {
            if (!s_ctx.has_target) {
                s_stats.errors++;
                continue;
            }

            int ret = sendto(sock, item.data, item.len, 0,
                             (struct sockaddr *)&s_ctx.target_addr,
                             sizeof(s_ctx.target_addr));

            if (ret > 0) {
                s_stats.fragments_forwarded++;
                s_stats.bytes_forwarded += ret;
            } else {
                s_stats.errors++;
                ESP_LOGW(TAG, "sendto failed: errno=%d", errno);
            }
        }
    }

    close(sock);
}

/* ============================================================================
 * API 实现
 * ==========================================================================*/
esp_err_t wifi_stream_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));

    /* 创建分片队列 (深度 64 = ~2 帧的缓冲) */
    s_ctx.frag_queue = xQueueCreate(64, sizeof(frag_item_t));
    if (s_ctx.frag_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* 创建发送任务 */
    xTaskCreate(wifi_stream_tx_task, "wifi_stream_tx", 4096, NULL, 5, &s_ctx.tx_task);

    ESP_LOGI(TAG, "Wi-Fi stream initialized");
    return ESP_OK;
}

esp_err_t wifi_stream_set_target(const char *ip_addr, uint16_t port)
{
    if (ip_addr == NULL) {
        s_ctx.has_target = false;
        return ESP_OK;
    }

    memset(&s_ctx.target_addr, 0, sizeof(s_ctx.target_addr));
    s_ctx.target_addr.sin6_family = AF_INET6;
    s_ctx.target_addr.sin6_port = htons(port != 0 ? port : WIFI_STREAM_UDP_PORT);

    /* 支持 IPv4/IPv6 双栈 */
    if (strchr(ip_addr, ':') != NULL) {
        /* IPv6 */
        inet_pton(AF_INET6, ip_addr, &s_ctx.target_addr.sin6_addr);
    } else {
        /* IPv4 → 映射到 IPv6 */
        struct in_addr ipv4;
        inet_pton(AF_INET, ip_addr, &ipv4);
        memcpy(&s_ctx.target_addr.sin6_addr.s6_addr[12], &ipv4, 4);
        s_ctx.target_addr.sin6_addr.s6_addr[10] = 0xFF;
        s_ctx.target_addr.sin6_addr.s6_addr[11] = 0xFF;
    }

    s_ctx.has_target = true;
    ESP_LOGI(TAG, "Target set to %s:%u", ip_addr, port);
    return ESP_OK;
}

esp_err_t wifi_stream_forward_fragment(const uint8_t *data, uint16_t len, bool broadcast)
{
    if (data == NULL || len == 0 || len > 1536) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)broadcast; /* 后续支持多播 */

    frag_item_t item;
    item.len = (len > sizeof(item.data)) ? sizeof(item.data) : len;
    memcpy(item.data, data, item.len);

    if (xQueueSend(s_ctx.frag_queue, &item, pdMS_TO_TICKS(10)) != pdPASS) {
        s_stats.errors++;
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t wifi_stream_get_stats(wifi_stream_stats_t *stats)
{
    if (stats == NULL) return ESP_ERR_INVALID_ARG;
    memcpy(stats, &s_stats, sizeof(wifi_stream_stats_t));

    /* 计算速率 (简单估算) */
    stats->last_rate_bps = s_stats.bytes_forwarded * 8 /
                           (uint32_t)((xTaskGetTickCount() * portTICK_PERIOD_MS + 1) / 1000);
    return ESP_OK;
}
