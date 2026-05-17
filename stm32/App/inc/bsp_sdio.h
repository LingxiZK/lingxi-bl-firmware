/**
  ******************************************************************************
  * @file    bsp_sdio.h
  * @brief   SDIO Host 驱动头文件 (ESP32-C6 Slave, PC8-PC12 + PD2)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - SDIO 1-bit/4-bit 模式, 25MHz max
  * - 自定义轻量协议 over SDIO
  * - DMA 传输, 双缓冲队列
  ******************************************************************************
  */

#ifndef __BSP_SDIO_H
#define __BSP_SDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * SDIO 配置参数
 * ==========================================================================*/
#define SDIO_CLK_DIV                2       /* HCLK/2 = 400MHz/2 = 200MHz, 实际 25-50MHz */
#define SDIO_BUS_WIDTH              4       /* 4-bit 模式 */
#define SDIO_BLOCK_SIZE             512     /* 块大小 */

/* 传输超时 */
#define SDIO_TX_TIMEOUT_MS          1000
#define SDIO_RX_TIMEOUT_MS          1000

/* 队列深度 */
#define SDIO_TX_QUEUE_DEPTH         8
#define SDIO_RX_QUEUE_DEPTH         8
#define SDIO_MAX_PACKET_SIZE        1536    /* 最大包大小 */

/* =============================================================================
 * 数据结构
 * ==========================================================================*/

typedef enum {
    SDIO_PKT_CMD    = 0x01,     /* 命令包 */
    SDIO_PKT_DATA   = 0x02,     /* 数据包 */
    SDIO_PKT_ACK    = 0x03,     /* 确认包 */
    SDIO_PKT_NACK   = 0x04,     /* 否定确认 */
    SDIO_PKT_HEART  = 0x05,     /* 心跳包 */
    SDIO_PKT_CTRL_CMD = 0x06,  /* 控制指令包 (vTaskControl -> ESP32) */
    SDIO_PKT_LOG    = 0x07,    /* 日志/诊断包 (vTaskLogger/vTaskDiag -> ESP32) */
    SDIO_PKT_IMAGE_FRAME   = 0x08,    /* 图像帧数据 (vTaskCamera -> ESP32 -> Wi-Fi -> 地面站) */
    SDIO_PKT_IMAGE_REQ    = 0x09,    /* 图像帧请求 (地面站 -> ESP32 -> STM32N6, 触发拍照) */
} sdio_pkt_type_t;

typedef struct __attribute__((packed)) {
    uint8_t  magic;             /* 0xA5 */
    uint8_t  type;              /* sdio_pkt_type_t */
    uint16_t seq;               /* 序列号 */
    uint16_t len;               /* 数据长度 */
    uint16_t crc16;             /* CRC16 */
    uint8_t  data[SDIO_MAX_PACKET_SIZE];
} sdio_packet_t;

typedef struct {
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t crc_errors;
    uint32_t timeout_errors;
} sdio_stats_t;

/* 回调函数类型 */
typedef void (*sdio_rx_callback_t)(const sdio_packet_t *pkt);

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 SDIO Host 接口
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdio_init(void);

/**
 * @brief  注册接收回调函数
 * @param  callback: 回调函数指针
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdio_register_rx_callback(sdio_rx_callback_t callback);

/**
 * @brief  发送数据包
 * @param  pkt: 数据包指针
 * @param  timeout_ms: 超时时间 (ms)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdio_send_packet(const sdio_packet_t *pkt, uint32_t timeout_ms);

/**
 * @brief  接收数据包 (阻塞式)
 * @param  pkt: 接收缓冲区
 * @param  timeout_ms: 超时时间 (ms)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdio_recv_packet(sdio_packet_t *pkt, uint32_t timeout_ms);

/**
 * @brief  获取 SDIO 统计信息
 * @param  stats: 统计信息结构体指针
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdio_get_stats(sdio_stats_t *stats);

/**
 * @brief  SDIO 中断服务程序
 */
void bsp_sdio_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SDIO_H */
