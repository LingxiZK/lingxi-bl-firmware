/**
  ******************************************************************************
  * @file    bsp_sdio.c
  * @brief   SDIO Host 驱动实现 (ESP32-C6 Slave)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 使用 SDMMC1 外设作为 SDIO Host
  * - DMA 传输, 中断驱动
  * - 自定义轻量协议封装
  ******************************************************************************
  */

#include "bsp_sdio.h"
#include "bsp_sdram.h"

/* =============================================================================
 * 私有宏与常量
 * ==========================================================================*/
#define SDIO_MAGIC_BYTE             0xA5
#define SDIO_QUEUE_ITEM_SIZE        sizeof(sdio_packet_t)

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static SDMMC_HandleTypeDef hsdmmc;
static DMA_HandleTypeDef hDMA_SDIO_TX;
static DMA_HandleTypeDef hDMA_SDIO_RX;

static QueueHandle_t s_tx_queue = NULL;
static QueueHandle_t s_rx_queue = NULL;
static SemaphoreHandle_t s_tx_sem = NULL;
static SemaphoreHandle_t s_rx_sem = NULL;

static sdio_rx_callback_t s_rx_callback = NULL;
static sdio_stats_t s_stats = {0};
static volatile uint8_t s_sdio_initialized = 0;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void sdio_gpio_init(void);
static void sdio_clock_init(void);
static lingxi_err_t sdio_dma_init(void);
static uint16_t sdio_crc16(const uint8_t *data, uint16_t len);
static lingxi_err_t sdio_send_internal(const sdio_packet_t *pkt);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 SDIO Host
 */
lingxi_err_t bsp_sdio_init(void)
{
    if (s_sdio_initialized) {
        return LINGXI_ERR_BUSY;
    }

    /* 创建队列和信号量 */
    s_tx_queue = xQueueCreate(SDIO_TX_QUEUE_DEPTH, SDIO_QUEUE_ITEM_SIZE);
    s_rx_queue = xQueueCreate(SDIO_RX_QUEUE_DEPTH, SDIO_QUEUE_ITEM_SIZE);
    s_tx_sem = xSemaphoreCreateBinary();
    s_rx_sem = xSemaphoreCreateBinary();

    if (s_tx_queue == NULL || s_rx_queue == NULL ||
        s_tx_sem == NULL || s_rx_sem == NULL) {
        return LINGXI_ERR_NO_MEM;
    }

    /* 时钟初始化 */
    sdio_clock_init();

    /* GPIO 初始化 */
    sdio_gpio_init();

    /* SDMMC 初始化 */
    hsdmmc.Instance = SDMMC1;
    hsdmmc.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsdmmc.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsdmmc.Init.BusWide = (SDIO_BUS_WIDTH == 4) ? SDMMC_BUS_WIDE_4B : SDMMC_BUS_WIDE_1B;
    hsdmmc.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsdmmc.Init.ClockDiv = SDIO_CLK_DIV;

    if (HAL_SD_Init(&hsdmmc) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* DMA 初始化 */
    lingxi_err_t err = sdio_dma_init();
    LX_RETURN_IF_ERR(err);

    /* 使能中断 */
    HAL_NVIC_SetPriority(SDMMC1_IRQn, IRQ_PRIO_CRITICAL, 0);
    HAL_NVIC_EnableIRQ(SDMMC1_IRQn);

    s_sdio_initialized = 1;

    LX_DEBUG_PRINT("SDIO Host initialized, %d-bit mode", SDIO_BUS_WIDTH);
    return LINGXI_OK;
}

/**
 * @brief  注册接收回调
 */
lingxi_err_t bsp_sdio_register_rx_callback(sdio_rx_callback_t callback)
{
    if (callback == NULL) {
        return LINGXI_ERR_INVALID_PARAM;
    }
    s_rx_callback = callback;
    return LINGXI_OK;
}

/**
 * @brief  发送数据包
 */
lingxi_err_t bsp_sdio_send_packet(const sdio_packet_t *pkt, uint32_t timeout_ms)
{
    LX_RETURN_IF_NULL(pkt);

    if (!s_sdio_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    /* 发送到队列 */
    if (xQueueSend(s_tx_queue, pkt, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        s_stats.tx_errors++;
        return LINGXI_ERR_TIMEOUT;
    }

    /* 触发发送 (如果 DMA 空闲) */
    if (uxSemaphoreGetCount(s_tx_sem) == 0) {
        xSemaphoreGive(s_tx_sem);
    }

    return LINGXI_OK;
}

/**
 * @brief  接收数据包 (阻塞式)
 */
lingxi_err_t bsp_sdio_recv_packet(sdio_packet_t *pkt, uint32_t timeout_ms)
{
    LX_RETURN_IF_NULL(pkt);

    if (!s_sdio_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    if (xQueueReceive(s_rx_queue, pkt, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return LINGXI_ERR_TIMEOUT;
    }

    return LINGXI_OK;
}

/**
 * @brief  获取统计信息
 */
lingxi_err_t bsp_sdio_get_stats(sdio_stats_t *stats)
{
    LX_RETURN_IF_NULL(stats);
    memcpy(stats, &s_stats, sizeof(sdio_stats_t));
    return LINGXI_OK;
}

/* =============================================================================
 * 中断与回调
 * ==========================================================================*/

/**
 * @brief  SDIO 中断服务程序
 */
LX_ISR_FUNC void bsp_sdio_isr(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    HAL_SD_IRQHandler(&hsdmmc);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  SDIO TX DMA 完成回调
 */
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    s_stats.tx_packets++;
    xSemaphoreGiveFromISR(s_tx_sem, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  SDIO RX DMA 完成回调
 */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    s_stats.rx_packets++;
    xSemaphoreGiveFromISR(s_rx_sem, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  SDIO 错误回调
 */
void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    s_stats.rx_errors++;
    LX_ERR_PRINT("SDIO error occurred");
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  SDIO GPIO 初始化
 */
static void sdio_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PC8-PC12: D0-D3 + CLK + CMD */
    GPIO_InitStruct.Pin = SDIO_D0_PIN | SDIO_D1_PIN | SDIO_D2_PIN |
                          SDIO_CLK_PIN | SDIO_CMD_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
    HAL_GPIO_Init(SDIO_GPIO_PORT_D0_D2, &GPIO_InitStruct);

    /* PD2: D3 */
    GPIO_InitStruct.Pin = SDIO_D3_PIN;
    HAL_GPIO_Init(SDIO_GPIO_PORT_D3, &GPIO_InitStruct);
}

/**
 * @brief  SDIO 时钟初始化
 */
static void sdio_clock_init(void)
{
    __HAL_RCC_SDMMC1_CLK_ENABLE();
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __HAL_RCC_SDMMC1_RELEASE_RESET();
}

/**
 * @brief  SDIO DMA 初始化
 */
static lingxi_err_t sdio_dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* TX DMA */
    hDMA_SDIO_TX.Instance = DMA1_Stream0;
    hDMA_SDIO_TX.Init.Request = DMA_REQUEST_SDMMC1;
    hDMA_SDIO_TX.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hDMA_SDIO_TX.Init.PeriphInc = DMA_PINC_DISABLE;
    hDMA_SDIO_TX.Init.MemInc = DMA_MINC_ENABLE;
    hDMA_SDIO_TX.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hDMA_SDIO_TX.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hDMA_SDIO_TX.Init.Mode = DMA_NORMAL;
    hDMA_SDIO_TX.Init.Priority = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&hDMA_SDIO_TX) != HAL_OK) {
        return LINGXI_ERR_DMA;
    }

    __HAL_LINKDMA(&hsdmmc, hdmatx, hDMA_SDIO_TX);

    /* RX DMA */
    hDMA_SDIO_RX.Instance = DMA1_Stream1;
    hDMA_SDIO_RX.Init.Request = DMA_REQUEST_SDMMC1;
    hDMA_SDIO_RX.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hDMA_SDIO_RX.Init.PeriphInc = DMA_PINC_DISABLE;
    hDMA_SDIO_RX.Init.MemInc = DMA_MINC_ENABLE;
    hDMA_SDIO_RX.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hDMA_SDIO_RX.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hDMA_SDIO_RX.Init.Mode = DMA_NORMAL;
    hDMA_SDIO_RX.Init.Priority = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&hDMA_SDIO_RX) != HAL_OK) {
        return LINGXI_ERR_DMA;
    }

    __HAL_LINKDMA(&hsdmmc, hdmarx, hDMA_SDIO_RX);

    return LINGXI_OK;
}

/**
 * @brief  CRC16 计算 (CCITT-FALSE)
 */
static uint16_t sdio_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief  内部发送函数
 */
static lingxi_err_t sdio_send_internal(const sdio_packet_t *pkt)
{
    /* 计算 CRC */
    sdio_packet_t tx_pkt;
    memcpy(&tx_pkt, pkt, sizeof(sdio_packet_t));
    tx_pkt.magic = SDIO_MAGIC_BYTE;
    tx_pkt.crc16 = sdio_crc16(tx_pkt.data, tx_pkt.len);

    /* DMA 发送 */
    HAL_StatusTypeDef status = HAL_SD_WriteBlocks_DMA(
        &hsdmmc,
        (uint8_t *)&tx_pkt,
        0, /* 块地址 (自定义协议不使用标准 SD 块地址) */
        1  /* 块数 */
    );

    if (status != HAL_OK) {
        s_stats.tx_errors++;
        return LINGXI_ERR_IO;
    }

    /* 等待 DMA 完成 */
    if (xSemaphoreTake(s_tx_sem, pdMS_TO_TICKS(SDIO_TX_TIMEOUT_MS)) != pdTRUE) {
        s_stats.timeout_errors++;
        return LINGXI_ERR_TIMEOUT;
    }

    s_stats.tx_bytes += tx_pkt.len + 8; /* header + data */
    return LINGXI_OK;
}
