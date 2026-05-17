/**
  ******************************************************************************
  * @file    sdmmc_driver.h
  * @brief   SDMMC1 SDIO Host 驱动头文件 (STM32N657L0H3Q) — P0批次
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - SDMMC1 作为 SDIO Host, 4-bit 模式
  * - 引脚: CLK(PC12), CMD(PC11), D0(PC8), D1(PC9), D2(PC10), D3(PD2)
  * - 时钟 25-50MHz, 与 ESP32-C6 SDIO Slave 通信
  * - DMA 传输, 中断驱动
  ******************************************************************************
  */

#ifndef __SDMMC_DRIVER_H
#define __SDMMC_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =============================================================================
 * SDIO 配置参数
 * ==========================================================================*/
#define SDMMC_CLK_DIV               4       /* HCLK/4 = 200MHz/4 = 50MHz */
#define SDMMC_BUS_WIDTH             4       /* 4-bit 模式 */
#define SDMMC_BLOCK_SIZE            512     /* 标准块大小 */

/* 超时配置 */
#define SDMMC_CMD_TIMEOUT_MS        1000
#define SDMMC_TX_TIMEOUT_MS         5000
#define SDMMC_RX_TIMEOUT_MS         5000
#define SDMMC_INIT_TIMEOUT_MS       2000

/* DMA 配置 */
#define SDMMC_DMA_TX_STREAM         DMA1_Stream0
#define SDMMC_DMA_RX_STREAM         DMA1_Stream1
#define SDMMC_DMA_TX_CHANNEL        DMA_REQUEST_SDMMC1
#define SDMMC_DMA_RX_CHANNEL        DMA_REQUEST_SDMMC1

/* =============================================================================
 * 错误码定义
 * ==========================================================================*/
typedef enum {
    SDMMC_OK                    = 0,
    SDMMC_ERR_NOT_INIT          = -1,
    SDMMC_ERR_INVALID_PARAM     = -2,
    SDMMC_ERR_TIMEOUT           = -3,
    SDMMC_ERR_DMA               = -4,
    SDMMC_ERR_HARDWARE          = -5,
    SDMMC_ERR_NO_CARD           = -6,
    SDMMC_ERR_CRC               = -7,
    SDMMC_ERR_BUSY              = -8,
} sdmmc_err_t;

/* =============================================================================
 * 数据结构
 * ==========================================================================*/

typedef enum {
    SDMMC_STATE_IDLE              = 0,
    SDMMC_STATE_INIT              = 1,
    SDMMC_STATE_READY             = 2,
    SDMMC_STATE_TX                = 3,
    SDMMC_STATE_RX                = 4,
    SDMMC_STATE_ERROR             = 5,
} sdmmc_state_t;

typedef struct {
    uint32_t tx_blocks;
    uint32_t rx_blocks;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t crc_errors;
    uint32_t timeout_errors;
    uint32_t dma_errors;
} sdmmc_stats_t;

/* SDIO 命令/响应结构 */
typedef struct {
    uint32_t cmd_index;
    uint32_t argument;
    uint32_t response[4];
    uint32_t flags;
} sdmmc_cmd_t;

/* 数据块结构 */
typedef struct {
    uint8_t *buffer;
    uint32_t block_addr;
    uint32_t block_count;
    uint32_t block_size;
} sdmmc_data_t;

/* =============================================================================
 * 回调函数类型
 * ==========================================================================*/
typedef void (*sdmmc_tx_complete_cb_t)(void);
typedef void (*sdmmc_rx_complete_cb_t)(void);
typedef void (*sdmmc_error_cb_t)(uint32_t error_code);

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 SDMMC1 SDIO Host 接口
 * @retval SDMMC_OK 成功, 其他错误码
 */
sdmmc_err_t sdmmc_driver_init(void);

/**
 * @brief  反初始化 SDMMC1
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_driver_deinit(void);

/**
 * @brief  发送 SDIO 命令
 * @param  cmd: 命令结构体指针
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_send_command(sdmmc_cmd_t *cmd);

/**
 * @brief  发送数据块 (DMA)
 * @param  data: 数据结构体指针
 * @param  timeout_ms: 超时时间
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_send_data_dma(sdmmc_data_t *data, uint32_t timeout_ms);

/**
 * @brief  接收数据块 (DMA)
 * @param  data: 数据结构体指针
 * @param  timeout_ms: 超时时间
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_recv_data_dma(sdmmc_data_t *data, uint32_t timeout_ms);

/**
 * @brief  设置 SDIO 总线宽度
 * @param  width: 1 或 4
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_set_bus_width(uint8_t width);

/**
 * @brief  设置 SDIO 时钟频率
 * @param  freq_hz: 目标频率 (Hz)
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_set_clock(uint32_t freq_hz);

/**
 * @brief  注册传输完成回调
 * @param  tx_cb: 发送完成回调
 * @param  rx_cb: 接收完成回调
 * @param  err_cb: 错误回调
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_register_callbacks(
    sdmmc_tx_complete_cb_t tx_cb,
    sdmmc_rx_complete_cb_t rx_cb,
    sdmmc_error_cb_t err_cb
);

/**
 * @brief  获取当前状态
 * @retval 当前状态
 */
sdmmc_state_t sdmmc_get_state(void);

/**
 * @brief  获取统计信息
 * @param  stats: 统计信息结构体指针
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_get_stats(sdmmc_stats_t *stats);

/**
 * @brief  SDMMC1 中断服务程序
 * @note   在 stm32n6xx_it.c 中调用
 */
void sdmmc_driver_isr(void);

/**
 * @brief  复位 SDMMC1 外设
 * @retval SDMMC_OK 成功
 */
sdmmc_err_t sdmmc_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __SDMMC_DRIVER_H */
