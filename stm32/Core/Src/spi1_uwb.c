/**
  ******************************************************************************
  * @file    spi1_uwb.c
  * @brief   SPI1 HAL Driver Implementation (DWM3000 UWB, STM32N657)
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
  *
  * @decision_record
  * - DR-001: 软件 NSS 控制, 支持多字节事务中保持 CS 低电平
  * - DR-002: SPI 使用 8-bit 数据宽度, MSB first, 与 DW3000 兼容
  * - DR-003: 所有传输函数在内部管理 NSS, 无需调用者干预
  * - DR-004: 支持 header+data 分离传输模式 (DW3000 寄存器操作常用)
  ******************************************************************************
  */

#include "spi1_uwb.h"

/* =============================================================================
 * 私有宏
 * ==========================================================================*/
#define SPI1_UWB_ASSERT(cond)       do { if (!(cond)) return SPI1_UWB_ERR_INVALID_PARAM; } while(0)
#define SPI1_UWB_CHECK_HANDLE(h)  do { if ((h) == NULL || (h)->hspi == NULL) return SPI1_UWB_ERR_NULL_PTR; } while(0)
#define SPI1_UWB_CHECK_INIT(h)    do { if ((h)->state != SPI1_UWB_STATE_INIT && \
                                             (h)->state != SPI1_UWB_STATE_READY && \
                                             (h)->state != SPI1_UWB_STATE_BUSY_TX && \
                                             (h)->state != SPI1_UWB_STATE_BUSY_RX && \
                                             (h)->state != SPI1_UWB_STATE_BUSY_TXRX) \
                                            return SPI1_UWB_ERR_NOT_INIT; } while(0)

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static const char *s_err_strings[] = {
    "OK",
    "NULL pointer",
    "Invalid parameter",
    "Timeout",
    "Hardware init failed",
    "SPI error",
    "Not initialized",
    "Busy",
    "DMA error",
    "IRQ error",
};

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static spi1_uwb_err_t spi1_uwb_gpio_init(void);
static spi1_uwb_err_t spi1_uwb_clock_init(void);
static spi1_uwb_err_t spi1_uwb_hal_init(spi1_uwb_handle_t *huwb);
static void spi1_uwb_update_tx_stats(spi1_uwb_handle_t *huwb, uint32_t start_tick);
static void spi1_uwb_update_rx_stats(spi1_uwb_handle_t *huwb, uint32_t start_tick);

/* =============================================================================
 * API 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 SPI1 UWB 驱动
 */
spi1_uwb_err_t spi1_uwb_init(spi1_uwb_handle_t *huwb, SPI_HandleTypeDef *hspi)
{
    SPI1_UWB_CHECK_HANDLE(hspi);

    if (huwb == NULL) {
        return SPI1_UWB_ERR_NULL_PTR;
    }

    /* 清零句柄 */
    memset(huwb, 0, sizeof(spi1_uwb_handle_t));
    huwb->hspi = hspi;
    huwb->state = SPI1_UWB_STATE_RESET;

    /* 时钟初始化 */
    spi1_uwb_err_t err = spi1_uwb_clock_init();
    if (err != SPI1_UWB_OK) {
        huwb->state = SPI1_UWB_STATE_ERROR;
        return err;
    }

    /* GPIO 初始化 */
    err = spi1_uwb_gpio_init();
    if (err != SPI1_UWB_OK) {
        huwb->state = SPI1_UWB_STATE_ERROR;
        return err;
    }

    /* HAL 初始化 */
    err = spi1_uwb_hal_init(huwb);
    if (err != SPI1_UWB_OK) {
        huwb->state = SPI1_UWB_STATE_ERROR;
        return err;
    }

    /* 释放 NSS */
    spi1_uwb_nss_release(huwb);

    /* 复位设备 */
    err = spi1_uwb_reset_device(huwb);
    if (err != SPI1_UWB_OK) {
        huwb->state = SPI1_UWB_STATE_ERROR;
        return err;
    }

    huwb->state = SPI1_UWB_STATE_INIT;

    return SPI1_UWB_OK;
}

/**
 * @brief  反初始化 SPI1 UWB 驱动
 */
spi1_uwb_err_t spi1_uwb_deinit(spi1_uwb_handle_t *huwb)
{
    SPI1_UWB_CHECK_HANDLE(huwb);

    /* 禁用中断 */
    spi1_uwb_disable_irq(huwb);

    /* 反初始化 HAL */
    HAL_SPI_DeInit(huwb->hspi);

    /* 关闭时钟 */
    __HAL_RCC_SPI1_CLK_DISABLE();

    huwb->state = SPI1_UWB_STATE_RESET;
    return SPI1_UWB_OK;
}

/**
 * @brief  SPI 发送数据 (阻塞式)
 */
spi1_uwb_err_t spi1_uwb_send(spi1_uwb_handle_t *huwb, const uint8_t *tx_buf, uint16_t len, uint32_t timeout_ms)
{
    SPI1_UWB_CHECK_HANDLE(huwb);
    SPI1_UWB_CHECK_INIT(huwb);
    SPI1_UWB_ASSERT(tx_buf != NULL);
    SPI1_UWB_ASSERT(len > 0);

    if (huwb->busy) {
        return SPI1_UWB_ERR_BUSY;
    }

    huwb->busy = 1;
    huwb->state = SPI1_UWB_STATE_BUSY_TX;

    uint32_t start = HAL_GetTick();

    spi1_uwb_nss_assert(huwb);

    HAL_StatusTypeDef hal_status = HAL_SPI_Transmit(huwb->hspi, (uint8_t *)tx_buf, len, timeout_ms);

    spi1_uwb_nss_release(huwb);

    huwb->busy = 0;

    if (hal_status != HAL_OK) {
        huwb->state = SPI1_UWB_STATE_READY;
        huwb->stats.err_count++;
        return SPI1_UWB_ERR_SPI;
    }

    huwb->state = SPI1_UWB_STATE_READY;
    huwb->stats.tx_count++;
    spi1_uwb_update_tx_stats(huwb, start);

    return SPI1_UWB_OK;
}

/**
 * @brief  SPI 接收数据 (阻塞式)
 */
spi1_uwb_err_t spi1_uwb_recv(spi1_uwb_handle_t *huwb, uint8_t *rx_buf, uint16_t len, uint32_t timeout_ms)
{
    SPI1_UWB_CHECK_HANDLE(huwb);
    SPI1_UWB_CHECK_INIT(huwb);
    SPI1_UWB_ASSERT(rx_buf != NULL);
    SPI1_UWB_ASSERT(len > 0);

    if (huwb->busy) {
        return SPI1_UWB_ERR_BUSY;
    }

    huwb->busy = 1;
    huwb->state = SPI1_UWB_STATE_BUSY_RX;

    uint32_t start = HAL_GetTick();

    spi1_uwb_nss_assert(huwb);

    HAL_StatusTypeDef hal_status = HAL_SPI_Receive(huwb->hspi, rx_buf, len, timeout_ms);

    spi1_uwb_nss_release(huwb);

    huwb->busy = 0;

    if (hal_status != HAL_OK) {
        huwb->state = SPI1_UWB_STATE_READY;
        huwb->stats.err_count++;
        return SPI1_UWB_ERR_SPI;
    }

    huwb->state = SPI1_UWB_STATE_READY;
    huwb->stats.rx_count++;
    spi1_uwb_update_rx_stats(huwb, start);

    return SPI1_UWB_OK;
}

/**
 * @brief  SPI 全双工发送+接收 (阻塞式)
 */
spi1_uwb_err_t spi1_uwb_send_recv(spi1_uwb_handle_t *huwb,
                                   const uint8_t *tx_buf, uint8_t *rx_buf,
                                   uint16_t len, uint32_t timeout_ms)
{
    SPI1_UWB_CHECK_HANDLE(huwb);
    SPI1_UWB_CHECK_INIT(huwb);
    SPI1_UWB_ASSERT(tx_buf != NULL);
    SPI1_UWB_ASSERT(rx_buf != NULL);
    SPI1_UWB_ASSERT(len > 0);

    if (huwb->busy) {
        return SPI1_UWB_ERR_BUSY;
    }

    huwb->busy = 1;
    huwb->state = SPI1_UWB_STATE_BUSY_TXRX;

    uint32_t start = HAL_GetTick();

    spi1_uwb_nss_assert(huwb);

    HAL_StatusTypeDef hal_status = HAL_SPI_TransmitReceive(huwb->hspi,
                                                               (uint8_t *)tx_buf, rx_buf,
                                                               len, timeout_ms);

    spi1_uwb_nss_release(huwb);

    huwb->busy = 0;

    if (hal_status != HAL_OK) {
        huwb->state = SPI1_UWB_STATE_READY;
        huwb->stats.err_count++;
        return SPI1_UWB_ERR_SPI;
    }

    huwb->state = SPI1_UWB_STATE_READY;
    huwb->stats.txrx_count++;

    uint32_t elapsed = HAL_GetTick() - start;
    if (elapsed > huwb->stats.max_tx_time_us) {
        huwb->stats.max_tx_time_us = elapsed;
    }

    return SPI1_UWB_OK;
}

/**
 * @brief  SPI 发送头+接收数据 (DW3000 读寄存器模式)
 */
spi1_uwb_err_t spi1_uwb_xfer_header_rx(spi1_uwb_handle_t *huwb,
                                        const uint8_t *tx_header, uint16_t header_len,
                                        uint8_t *rx_buf, uint16_t rx_len,
                                        uint32_t timeout_ms)
{
    SPI1_UWB_CHECK_HANDLE(huwb);
    SPI1_UWB_CHECK_INIT(huwb);
    SPI1_UWB_ASSERT(tx_header != NULL);
    SPI1_UWB_ASSERT(rx_buf != NULL);
    SPI1_UWB_ASSERT(header_len > 0);

    if (huwb->busy) {
        return SPI1_UWB_ERR_BUSY;
    }

    huwb->busy = 1;
    huwb->state = SPI1_UWB_STATE_BUSY_TXRX;

    uint32_t start = HAL_GetTick();

    spi1_uwb_nss_assert(huwb);

    /* 发送 header */
    HAL_StatusTypeDef hal_status = HAL_SPI_Transmit(huwb->hspi, (uint8_t *)tx_header, header_len, timeout_ms);
    if (hal_status != HAL_OK) {
        spi1_uwb_nss_release(huwb);
        huwb->busy = 0;
        huwb->state = SPI1_UWB_STATE_READY;
        huwb->stats.err_count++;
        return SPI1_UWB_ERR_SPI;
    }

    /* 接收数据 */
    if (rx_len > 0) {
        hal_status = HAL_SPI_Receive(huwb->hspi, rx_buf, rx_len, timeout_ms);
        if (hal_status != HAL_OK) {
            spi1_uwb_nss_release(huwb);
            huwb->busy = 0;
            huwb->state = SPI1_UWB_STATE_READY;
            huwb->stats.err_count++;
            return SPI1_UWB_ERR_SPI;
        }
    }

    spi1_uwb_nss_release(huwb);

    huwb->busy = 0;
    huwb->state = SPI1_UWB_STATE_READY;
    huwb->stats.txrx_count++;

    uint32_t elapsed = HAL_GetTick() - start;
    if (elapsed > huwb->stats.max_rx_time_us) {
        huwb->stats.max_rx_time_us = elapsed;
    }

    return SPI1_UWB_OK;
}

/**
 * @brief  SPI 发送头+发送数据 (DW3000 写寄存器模式)
 */
spi1_uwb_err_t spi1_uwb_xfer_header_tx(spi1_uwb_handle_t *huwb,
                                        const uint8_t *tx_header, uint16_t header_len,
                                        const uint8_t *tx_buf, uint16_t tx_len,
                                        uint32_t timeout_ms)
{
    SPI1_UWB_CHECK_HANDLE(huwb);
    SPI1_UWB_CHECK_INIT(huwb);
    SPI1_UWB_ASSERT(tx_header != NULL);
    SPI1_UWB_ASSERT(tx_buf != NULL);
    SPI1_UWB_ASSERT(header_len > 0);

    if (huwb->busy) {
        return SPI1_UWB_ERR_BUSY;
    }

    huwb->busy = 1;
    huwb->state = SPI1_UWB_STATE_BUSY_TX;

    uint32_t start = HAL_GetTick();

    spi1_uwb_nss_assert(huwb);

    /* 发送 header */
    HAL_StatusTypeDef hal_status = HAL_SPI_Transmit(huwb->hspi, (uint8_t *)tx_header, header_len, timeout_ms);
    if (hal_status != HAL_OK) {
        spi1_uwb_nss_release(huwb);
        huwb->busy = 0;
        huwb->state = SPI1_UWB_STATE_READY;
        huwb->stats.err_count++;
        return SPI1_UWB_ERR_SPI;
    }

    /* 发送数据 */
    if (tx_len > 0) {
        hal_status = HAL_SPI_Transmit(huwb->hspi, (uint8_t *)tx_buf, tx_len, timeout_ms);
        if (hal_status != HAL_OK) {
            spi1_uwb_nss_release(huwb);
            huwb->busy = 0;
            huwb->state = SPI1_UWB_STATE_READY;
            huwb->stats.err_count++;
            return SPI1_UWB_ERR_SPI;
        }
    }

    spi1_uwb_nss_release(huwb);

    huwb->busy = 0;
    huwb->state = SPI1_UWB_STATE_READY;
    huwb->stats.tx_count++;
    spi1_uwb_update_tx_stats(huwb, start);

    return SPI1_UWB_OK;
}

/**
 * @brief  断言 NSS (片选低电平)
 */
void spi1_uwb_nss_assert(spi1_uwb_handle_t *huwb)
{
    if (huwb == NULL) {
        return;
    }
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_NSS_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  释放 NSS (片选高电平)
 */
void spi1_uwb_nss_release(spi1_uwb_handle_t *huwb)
{
    if (huwb == NULL) {
        return;
    }
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_NSS_PIN, GPIO_PIN_SET);
}

/**
 * @brief  复位 DWM3000 设备
 */
spi1_uwb_err_t spi1_uwb_reset_device(spi1_uwb_handle_t *huwb)
{
    SPI1_UWB_CHECK_HANDLE(huwb);

    /* 拉低 RST 至少 10us */
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(SPI1_UWB_RST_PULSE_MS);
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_RST_PIN, GPIO_PIN_SET);

    /* 等待设备启动 (典型 2ms) */
    HAL_Delay(3);

    huwb->stats.rst_count++;

    return SPI1_UWB_OK;
}

/**
 * @brief  唤醒 DWM3000 设备
 */
spi1_uwb_err_t spi1_uwb_wakeup_device(spi1_uwb_handle_t *huwb)
{
    SPI1_UWB_CHECK_HANDLE(huwb);

    /* 拉高 WAKEUP/SYNC 引脚 */
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_SYNC_PIN, GPIO_PIN_SET);
    HAL_Delay(SPI1_UWB_WAKEUP_DELAY_MS);
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_SYNC_PIN, GPIO_PIN_RESET);

    return SPI1_UWB_OK;
}

/**
 * @brief  进入 DWM3000 低功耗模式
 */
spi1_uwb_err_t spi1_uwb_sleep_device(spi1_uwb_handle_t *huwb)
{
    SPI1_UWB_CHECK_HANDLE(huwb);

    /* 通过 SPI 发送深度睡眠命令 */
    /* 具体命令参考 DW3000 用户手册 */
    /* 此处仅拉低 WAKEUP 引脚 */
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_SYNC_PIN, GPIO_PIN_RESET);

    return SPI1_UWB_OK;
}

/**
 * @brief  注册 IRQ 中断回调
 */
spi1_uwb_err_t spi1_uwb_register_irq_callback(spi1_uwb_handle_t *huwb,
                                               spi1_uwb_irq_callback_t callback,
                                               void *user_ctx)
{
    SPI1_UWB_CHECK_HANDLE(huwb);
    SPI1_UWB_ASSERT(callback != NULL);

    huwb->irq_cb = callback;
    huwb->irq_ctx = user_ctx;

    return SPI1_UWB_OK;
}

/**
 * @brief  使能 IRQ 中断
 */
spi1_uwb_err_t spi1_uwb_enable_irq(spi1_uwb_handle_t *huwb)
{
    SPI1_UWB_CHECK_HANDLE(huwb);

    HAL_NVIC_SetPriority(SPI1_UWB_IRQ_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(SPI1_UWB_IRQ_IRQn);

    return SPI1_UWB_OK;
}

/**
 * @brief  禁用 IRQ 中断
 */
spi1_uwb_err_t spi1_uwb_disable_irq(spi1_uwb_handle_t *huwb)
{
    SPI1_UWB_CHECK_HANDLE(huwb);

    HAL_NVIC_DisableIRQ(SPI1_UWB_IRQ_IRQn);

    return SPI1_UWB_OK;
}

/**
 * @brief  获取驱动状态
 */
spi1_uwb_state_t spi1_uwb_get_state(spi1_uwb_handle_t *huwb)
{
    if (huwb == NULL) {
        return SPI1_UWB_STATE_RESET;
    }
    return huwb->state;
}

/**
 * @brief  获取运行统计
 */
spi1_uwb_err_t spi1_uwb_get_stats(spi1_uwb_handle_t *huwb, spi1_uwb_stats_t *stats)
{
    SPI1_UWB_CHECK_HANDLE(huwb);
    SPI1_UWB_ASSERT(stats != NULL);

    memcpy(stats, &huwb->stats, sizeof(spi1_uwb_stats_t));
    return SPI1_UWB_OK;
}

/**
 * @brief  获取错误码描述字符串
 */
const char* spi1_uwb_err_to_string(spi1_uwb_err_t err)
{
    int idx = -err;
    if (idx >= 0 && idx < (int)(sizeof(s_err_strings) / sizeof(s_err_strings[0]))) {
        return s_err_strings[idx];
    }
    return "Unknown error";
}

/* =============================================================================
 * 中断处理函数
 * ==========================================================================*/

/**
 * @brief  UWB IRQ 中断处理
 */
void spi1_uwb_irq_handler(spi1_uwb_handle_t *huwb)
{
    if (huwb == NULL) {
        return;
    }

    huwb->stats.irq_count++;
    huwb->irq_pending = 1;

    /* 调用用户回调 */
    if (huwb->irq_cb != NULL) {
        huwb->irq_cb(huwb->irq_ctx);
    }

    huwb->irq_pending = 0;
}

/* =============================================================================
 * HAL 回调函数 (弱定义)
 * ==========================================================================*/

__weak void spi1_uwb_hal_tx_callback(SPI_HandleTypeDef *hspi)
{
    (void)hspi;
}

__weak void spi1_uwb_hal_rx_callback(SPI_HandleTypeDef *hspi)
{
    (void)hspi;
}

__weak void spi1_uwb_hal_txrx_callback(SPI_HandleTypeDef *hspi)
{
    (void)hspi;
}

__weak void spi1_uwb_hal_err_callback(SPI_HandleTypeDef *hspi)
{
    (void)hspi;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  GPIO 初始化
 */
static spi1_uwb_err_t spi1_uwb_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* SPI 引脚: PA5(SCK), PA6(MISO), PA7(MOSI) */
    GPIO_InitStruct.Pin = SPI1_UWB_SCK_PIN | SPI1_UWB_MISO_PIN | SPI1_UWB_MOSI_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = SPI1_UWB_GPIO_AF;
    HAL_GPIO_Init(SPI1_UWB_GPIO_PORT, &GPIO_InitStruct);

    /* NSS: PA4 (软件控制, 推挽输出) */
    GPIO_InitStruct.Pin = SPI1_UWB_NSS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SPI1_UWB_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_NSS_PIN, GPIO_PIN_SET);

    /* IRQ: PA8 (中断输入, 下降沿触发) */
    GPIO_InitStruct.Pin = SPI1_UWB_IRQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(SPI1_UWB_GPIO_PORT, &GPIO_InitStruct);

    /* RST: PA11 (推挽输出) */
    GPIO_InitStruct.Pin = SPI1_UWB_RST_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SPI1_UWB_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_RST_PIN, GPIO_PIN_SET);

    /* SYNC: PA12 (推挽输出, 预留) */
    GPIO_InitStruct.Pin = SPI1_UWB_SYNC_PIN;
    HAL_GPIO_Init(SPI1_UWB_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(SPI1_UWB_GPIO_PORT, SPI1_UWB_SYNC_PIN, GPIO_PIN_RESET);

    return SPI1_UWB_OK;
}

/**
 * @brief  时钟初始化
 */
static spi1_uwb_err_t spi1_uwb_clock_init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();

    return SPI1_UWB_OK;
}

/**
 * @brief  HAL 层初始化
 */
static spi1_uwb_err_t spi1_uwb_hal_init(spi1_uwb_handle_t *huwb)
{
    SPI_HandleTypeDef *hspi = huwb->hspi;

    hspi->Instance = SPI1;
    hspi->Init.Mode = SPI_MODE_MASTER;
    hspi->Init.Direction = SPI_DIRECTION_2LINES;
    hspi->Init.DataSize = SPI1_UWB_DATA_SIZE;
    hspi->Init.CLKPolarity = SPI1_UWB_CPOL;
    hspi->Init.CLKPhase = SPI1_UWB_CPHA;
    hspi->Init.NSS = SPI1_UWB_NSS_MODE;
    hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_80; /* 800MHz/80 = 10MHz */
    hspi->Init.FirstBit = SPI1_UWB_FIRST_BIT;
    hspi->Init.TIMode = SPI_TIMODE_DISABLE;
    hspi->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi->Init.CRCPolynomial = 7;
    hspi->Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    hspi->Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    hspi->Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    hspi->Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi->Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi->Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    hspi->Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hspi->Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi->Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    hspi->Init.IOSwap = SPI_IO_SWAP_DISABLE;
    hspi->Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
    hspi->Init.ReadyPolarity = SPI_RDY_POLARITY_LOW;

    if (HAL_SPI_Init(hspi) != HAL_OK) {
        return SPI1_UWB_ERR_HW_INIT;
    }

    return SPI1_UWB_OK;
}

/**
 * @brief  更新发送统计
 */
static void spi1_uwb_update_tx_stats(spi1_uwb_handle_t *huwb, uint32_t start_tick)
{
    uint32_t elapsed = HAL_GetTick() - start_tick;
    if (elapsed > huwb->stats.max_tx_time_us) {
        huwb->stats.max_tx_time_us = elapsed;
    }
}

/**
 * @brief  更新接收统计
 */
static void spi1_uwb_update_rx_stats(spi1_uwb_handle_t *huwb, uint32_t start_tick)
{
    uint32_t elapsed = HAL_GetTick() - start_tick;
    if (elapsed > huwb->stats.max_rx_time_us) {
        huwb->stats.max_rx_time_us = elapsed;
    }
}
