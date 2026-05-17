#pragma once
/**
 * @file sdio_slave.h
 * @brief ESP32-C6 SDIO Slave 驱动
 * @version 3.2
 *
 * SDIO Slave 模式，连接 STM32N657 SDMMC1 Host
 * - 4-bit 数据总线 (D0-D3)
 * - 25-50MHz 时钟
 * - 块大小 512 字节
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/sdio_slave.h"
#include "lingxi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * 配置常量
 *===========================================================================*/

#define SDIO_SLAVE_CLK_GPIO     6       /**< SDIO CLK (GPIO6) */
#define SDIO_SLAVE_CMD_GPIO     7       /**< SDIO CMD (GPIO7) */
#define SDIO_SLAVE_D0_GPIO      8       /**< SDIO D0 (GPIO8) */
#define SDIO_SLAVE_D1_GPIO      9       /**< SDIO D1 (GPIO9) */
#define SDIO_SLAVE_D2_GPIO      10      /**< SDIO D2 (GPIO10) */
#define SDIO_SLAVE_D3_GPIO      11      /**< SDIO D3 (GPIO11) */

#define SDIO_SLAVE_BLOCK_SIZE   512     /**< SDIO 块大小 */
#define SDIO_SLAVE_BUF_COUNT    8       /**< 接收缓冲区数量 */
#define SDIO_SLAVE_TX_QUEUE     8       /**< 发送队列深度 */
#define SDIO_SLAVE_RX_QUEUE     8       /**< 接收队列深度 */

#define SDIO_SLAVE_IRQ_GPIO     12      /**< 中断请求 GPIO (GPIO12 -> STM32 PC2) */
#define SDIO_SLAVE_RST_GPIO     13      /**< 复位控制 GPIO (GPIO13 <- STM32 PC0) */
#define SDIO_SLAVE_EN_GPIO      14      /**< 使能控制 GPIO (GPIO14 <- STM32 PC1) */

/*=============================================================================
 * 类型定义
 *===========================================================================*/

/**
 * @brief SDIO 事件类型
 */
typedef enum {
    SDIO_EVT_HOST_RESET = 0,    /**< Host 发起复位 */
    SDIO_EVT_HOST_ENABLE,       /**< Host 使能信号 */
    SDIO_EVT_TX_COMPLETE,       /**< 发送完成 */
    SDIO_EVT_RX_READY,          /**< 接收就绪 */
    SDIO_EVT_ERROR,             /**< 错误事件 */
} sdio_slave_event_t;

/**
 * @brief SDIO 事件回调
 */
typedef void (*sdio_slave_callback_t)(sdio_slave_event_t event, void *arg);

/**
 * @brief SDIO Slave 句柄
 */
typedef struct sdio_slave_handle_s *sdio_slave_handle_t;

/*=============================================================================
 * API 函数
 *===========================================================================*/

/**
 * @brief 初始化 SDIO Slave 驱动
 *
 * 配置 SDIO 引脚、DMA、中断，准备与 STM32 Host 通信
 *
 * @return ESP_OK 成功，其他错误码
 */
esp_err_t sdio_slave_init(void);

/**
 * @brief 反初始化 SDIO Slave
 */
esp_err_t sdio_slave_deinit(void);

/**
 * @brief 注册事件回调
 */
esp_err_t sdio_slave_register_callback(sdio_slave_callback_t cb, void *arg);

/**
 * @brief 发送协议帧到 Host
 *
 * @param frame  要发送的帧
 * @param timeout_ms 超时时间 (ms), -1 为无限等待
 * @return ESP_OK 成功
 */
esp_err_t sdio_slave_send_frame(const lx_frame_t *frame, int32_t timeout_ms);

/**
 * @brief 接收协议帧 (阻塞)
 *
 * @param frame  接收缓冲区
 * @param timeout_ms 超时时间 (ms), -1 为无限等待
 * @return ESP_OK 成功，ESP_ERR_TIMEOUT 超时
 */
esp_err_t sdio_slave_recv_frame(lx_frame_t *frame, int32_t timeout_ms);

/**
 * @brief 发送原始数据块 (用于图传等大流量数据)
 */
esp_err_t sdio_slave_send_raw(const uint8_t *data, uint16_t len, int32_t timeout_ms);

/**
 * @brief 触发中断给 Host (通知数据就绪)
 */
esp_err_t sdio_slave_trigger_irq(void);

/**
 * @brief 获取 SDIO 统计信息
 */
typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t irq_count;
} sdio_slave_stats_t;

esp_err_t sdio_slave_get_stats(sdio_slave_stats_t *stats);

/**
 * @brief 清除统计
 */
esp_err_t sdio_slave_clear_stats(void);

#ifdef __cplusplus
}
#endif
