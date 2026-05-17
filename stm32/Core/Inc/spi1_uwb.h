/**
  ******************************************************************************
  * @file    spi1_uwb.h
  * @brief   SPI1 HAL Driver Header (DWM3000 UWB, STM32N657)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - Target: STM32N657L0H3Q, SPI1
  * - Device: DWM3000 (Qorvo DW3000 UWB Transceiver)
  * - Clock: 10MHz, CPOL=0, CPHA=0
  * - Pin Mapping:
  *     SCK:  PA5
  *     MISO: PA6
  *     MOSI: PA7
  *     NSS:  PA4 (软件控制)
  *     IRQ:  PA8 (中断输入)
  *     RST:  PA11 (输出)
  *     SYNC: PA12 (输出, 预留)
  ******************************************************************************
  */

#ifndef __SPI1_UWB_H
#define __SPI1_UWB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * 版本与标识
 * ==========================================================================*/
#define SPI1_UWB_DRIVER_VERSION     0x0320  /* v3.2.0 */

/* =============================================================================
 * SPI1 配置参数
 * ==========================================================================*/
#define SPI1_UWB_BAUDRATE           10000000U       /* 10 MHz */
#define SPI1_UWB_DATA_SIZE          SPI_DATASIZE_8BIT
#define SPI1_UWB_CPOL               SPI_POLARITY_LOW
#define SPI1_UWB_CPHA               SPI_PHASE_1EDGE
#define SPI1_UWB_FIRST_BIT          SPI_FIRSTBIT_MSB
#define SPI1_UWB_NSS_MODE           SPI_NSS_SOFT    /* 软件NSS控制 */

/* =============================================================================
 * GPIO 引脚定义 (SPI1 on GPIOA)
 * ==========================================================================*/
#define SPI1_UWB_SCK_PIN            GPIO_PIN_5      /* PA5: SPI1_SCK  */
#define SPI1_UWB_MISO_PIN           GPIO_PIN_6      /* PA6: SPI1_MISO */
#define SPI1_UWB_MOSI_PIN           GPIO_PIN_7      /* PA7: SPI1_MOSI */
#define SPI1_UWB_NSS_PIN            GPIO_PIN_4      /* PA4: SPI1_NSS (软件) */
#define SPI1_UWB_IRQ_PIN            GPIO_PIN_8      /* PA8: UWB_IRQ   */
#define SPI1_UWB_RST_PIN            GPIO_PIN_11     /* PA11: UWB_RST  */
#define SPI1_UWB_SYNC_PIN           GPIO_PIN_12     /* PA12: UWB_SYNC */
#define SPI1_UWB_GPIO_PORT          GPIOA
#define SPI1_UWB_GPIO_AF            GPIO_AF5_SPI1

/* 中断线 */
#define SPI1_UWB_IRQ_EXTI_LINE      EXTI_LINE_8
#define SPI1_UWB_IRQ_IRQn           EXTI8_IRQn

/* =============================================================================
 * 超时配置
 * ==========================================================================*/
#define SPI1_UWB_INIT_TIMEOUT_MS    1000U
#define SPI1_UWB_TX_TIMEOUT_MS      100U
#define SPI1_UWB_RX_TIMEOUT_MS      100U
#define SPI1_UWB_TXRX_TIMEOUT_MS    200U
#define SPI1_UWB_RST_PULSE_MS       2U          /* 复位脉冲宽度 */
#define SPI1_UWB_RST_HOLD_US          10U         /* 复位保持时间 */
#define SPI1_UWB_WAKEUP_DELAY_MS    2U          /* 唤醒后等待 */

/* =============================================================================
 * 错误码定义
 * ==========================================================================*/
typedef enum {
    SPI1_UWB_OK = 0,
    SPI1_UWB_ERR_NULL_PTR = -1,
    SPI1_UWB_ERR_INVALID_PARAM = -2,
    SPI1_UWB_ERR_TIMEOUT = -3,
    SPI1_UWB_ERR_HW_INIT = -4,
    SPI1_UWB_ERR_SPI = -5,
    SPI1_UWB_ERR_NOT_INIT = -6,
    SPI1_UWB_ERR_BUSY = -7,
    SPI1_UWB_ERR_DMA = -8,
    SPI1_UWB_ERR_IRQ = -9,
} spi1_uwb_err_t;

/* =============================================================================
 * 驱动状态枚举
 * ==========================================================================*/
typedef enum {
    SPI1_UWB_STATE_RESET = 0,
    SPI1_UWB_STATE_INIT,
    SPI1_UWB_STATE_READY,
    SPI1_UWB_STATE_BUSY_TX,
    SPI1_UWB_STATE_BUSY_RX,
    SPI1_UWB_STATE_BUSY_TXRX,
    SPI1_UWB_STATE_ERROR,
} spi1_uwb_state_t;

/* =============================================================================
 * 传输模式枚举
 * ==========================================================================*/
typedef enum {
    SPI1_UWB_TX_ONLY = 0,           /* 仅发送 */
    SPI1_UWB_RX_ONLY,               /* 仅接收 */
    SPI1_UWB_TXRX,                  /* 全双工发送+接收 */
} spi1_uwb_xfer_mode_t;

/* =============================================================================
 * 运行统计结构体
 * ==========================================================================*/
typedef struct {
    uint32_t tx_count;              /* 发送次数 */
    uint32_t rx_count;              /* 接收次数 */
    uint32_t txrx_count;            /* 全双工次数 */
    uint32_t err_count;             /* 错误计数 */
    uint32_t irq_count;             /* 中断计数 */
    uint32_t rst_count;             /* 复位计数 */
    uint32_t max_tx_time_us;        /* 最大发送耗时 */
    uint32_t max_rx_time_us;        /* 最大接收耗时 */
} spi1_uwb_stats_t;

/* =============================================================================
 * 中断回调函数类型
 * ==========================================================================*/
typedef void (*spi1_uwb_irq_callback_t)(void *user_ctx);

/* =============================================================================
 * 驱动句柄结构体
 * ==========================================================================*/
typedef struct {
    SPI_HandleTypeDef       *hspi;          /* SPI HAL句柄 */
    spi1_uwb_state_t        state;          /* 驱动状态 */
    spi1_uwb_stats_t        stats;          /* 运行统计 */
    spi1_uwb_irq_callback_t irq_cb;         /* IRQ回调 */
    void                    *irq_ctx;       /* IRQ上下文 */
    volatile uint8_t        busy;           /* 忙标志 */
    volatile uint8_t        irq_pending;    /* IRQ挂起标志 */
} spi1_uwb_handle_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 SPI1 UWB 驱动
 * @param  huwb: 驱动句柄指针
 * @param  hspi: SPI HAL句柄指针
 * @retval SPI1_UWB_OK 成功, 其他错误码
 */
spi1_uwb_err_t spi1_uwb_init(spi1_uwb_handle_t *huwb, SPI_HandleTypeDef *hspi);

/**
 * @brief  反初始化 SPI1 UWB 驱动
 * @param  huwb: 驱动句柄指针
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_deinit(spi1_uwb_handle_t *huwb);

/**
 * @brief  SPI 发送数据 (阻塞式)
 * @param  huwb: 驱动句柄指针
 * @param  tx_buf: 发送缓冲区
 * @param  len: 发送长度
 * @param  timeout_ms: 超时时间(ms)
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_send(spi1_uwb_handle_t *huwb, const uint8_t *tx_buf, uint16_t len, uint32_t timeout_ms);

/**
 * @brief  SPI 接收数据 (阻塞式)
 * @param  huwb: 驱动句柄指针
 * @param  rx_buf: 接收缓冲区
 * @param  len: 接收长度
 * @param  timeout_ms: 超时时间(ms)
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_recv(spi1_uwb_handle_t *huwb, uint8_t *rx_buf, uint16_t len, uint32_t timeout_ms);

/**
 * @brief  SPI 全双工发送+接收 (阻塞式)
 * @param  huwb: 驱动句柄指针
 * @param  tx_buf: 发送缓冲区
 * @param  rx_buf: 接收缓冲区
 * @param  len: 传输长度
 * @param  timeout_ms: 超时时间(ms)
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_send_recv(spi1_uwb_handle_t *huwb,
                                   const uint8_t *tx_buf, uint8_t *rx_buf,
                                   uint16_t len, uint32_t timeout_ms);

/**
 * @brief  SPI 发送+接收 (常用寄存器操作模式)
 * @param  huwb: 驱动句柄指针
 * @param  tx_header: 发送头(通常1-2字节, 含读写标志和地址)
 * @param  header_len: 头长度
 * @param  rx_buf: 接收缓冲区
 * @param  rx_len: 接收长度
 * @param  timeout_ms: 超时时间(ms)
 * @retval SPI1_UWB_OK 成功
 * @note   先发送header, 然后接收数据 (CS保持低)
 */
spi1_uwb_err_t spi1_uwb_xfer_header_rx(spi1_uwb_handle_t *huwb,
                                        const uint8_t *tx_header, uint16_t header_len,
                                        uint8_t *rx_buf, uint16_t rx_len,
                                        uint32_t timeout_ms);

/**
 * @brief  SPI 发送头+发送数据 (写寄存器模式)
 * @param  huwb: 驱动句柄指针
 * @param  tx_header: 发送头
 * @param  header_len: 头长度
 * @param  tx_buf: 数据缓冲区
 * @param  tx_len: 数据长度
 * @param  timeout_ms: 超时时间(ms)
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_xfer_header_tx(spi1_uwb_handle_t *huwb,
                                        const uint8_t *tx_header, uint16_t header_len,
                                        const uint8_t *tx_buf, uint16_t tx_len,
                                        uint32_t timeout_ms);

/**
 * @brief  断言 NSS (片选低电平)
 * @param  huwb: 驱动句柄指针
 */
void spi1_uwb_nss_assert(spi1_uwb_handle_t *huwb);

/**
 * @brief  释放 NSS (片选高电平)
 * @param  huwb: 驱动句柄指针
 */
void spi1_uwb_nss_release(spi1_uwb_handle_t *huwb);

/**
 * @brief  复位 DWM3000 设备
 * @param  huwb: 驱动句柄指针
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_reset_device(spi1_uwb_handle_t *huwb);

/**
 * @brief  唤醒 DWM3000 设备
 * @param  huwb: 驱动句柄指针
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_wakeup_device(spi1_uwb_handle_t *huwb);

/**
 * @brief  进入 DWM3000 低功耗模式
 * @param  huwb: 驱动句柄指针
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_sleep_device(spi1_uwb_handle_t *huwb);

/**
 * @brief  注册 IRQ 中断回调
 * @param  huwb: 驱动句柄指针
 * @param  callback: 回调函数
 * @param  user_ctx: 用户上下文
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_register_irq_callback(spi1_uwb_handle_t *huwb,
                                               spi1_uwb_irq_callback_t callback,
                                               void *user_ctx);

/**
 * @brief  使能 IRQ 中断
 * @param  huwb: 驱动句柄指针
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_enable_irq(spi1_uwb_handle_t *huwb);

/**
 * @brief  禁用 IRQ 中断
 * @param  huwb: 驱动句柄指针
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_disable_irq(spi1_uwb_handle_t *huwb);

/**
 * @brief  获取驱动状态
 * @param  huwb: 驱动句柄指针
 * @retval 当前状态
 */
spi1_uwb_state_t spi1_uwb_get_state(spi1_uwb_handle_t *huwb);

/**
 * @brief  获取运行统计
 * @param  huwb: 驱动句柄指针
 * @param  stats: 输出统计结构体
 * @retval SPI1_UWB_OK 成功
 */
spi1_uwb_err_t spi1_uwb_get_stats(spi1_uwb_handle_t *huwb, spi1_uwb_stats_t *stats);

/**
 * @brief  获取错误码描述字符串
 * @param  err: 错误码
 * @retval 描述字符串
 */
const char* spi1_uwb_err_to_string(spi1_uwb_err_t err);

/* =============================================================================
 * 中断处理函数 (需在 ISR 中调用)
 * ==========================================================================*/

/**
 * @brief  UWB IRQ 中断处理
 * @param  huwb: 驱动句柄指针
 */
void spi1_uwb_irq_handler(spi1_uwb_handle_t *huwb);

/**
 * @brief  SPI 传输完成回调 (HAL 弱定义覆盖)
 * @param  hspi: SPI HAL句柄
 */
void spi1_uwb_hal_tx_callback(SPI_HandleTypeDef *hspi);

/**
 * @brief  SPI 接收完成回调 (HAL 弱定义覆盖)
 * @param  hspi: SPI HAL句柄
 */
void spi1_uwb_hal_rx_callback(SPI_HandleTypeDef *hspi);

/**
 * @brief  SPI 全双工完成回调 (HAL 弱定义覆盖)
 * @param  hspi: SPI HAL句柄
 */
void spi1_uwb_hal_txrx_callback(SPI_HandleTypeDef *hspi);

/**
 * @brief  SPI 错误回调 (HAL 弱定义覆盖)
 * @param  hspi: SPI HAL句柄
 */
void spi1_uwb_hal_err_callback(SPI_HandleTypeDef *hspi);

#ifdef __cplusplus
}
#endif

#endif /* __SPI1_UWB_H */
