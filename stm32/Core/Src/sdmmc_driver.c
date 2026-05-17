/**
  ******************************************************************************
  * @file    sdmmc_driver.c
  * @brief   SDMMC1 SDIO Host 驱动实现 (STM32N657L0H3Q) — P0批次
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - SDMMC1 作为 SDIO Host, 4-bit 模式
  * - 引脚: CLK(PC12), CMD(PC11), D0(PC8), D1(PC9), D2(PC10), D3(PD2)
  * - DMA 双缓冲传输, 中断驱动
  * - 与 ESP32-C6 SDIO Slave 通信
  ******************************************************************************
  */

#include "sdmmc_driver.h"
#include "rcc_config.h"
#include "gpio_uart.h"
#include "stm32n6xx_hal.h"

/* =============================================================================
 * 私有宏定义
 * ==========================================================================*/
#define SDMMC_MAX_RETRY             3
#define SDMMC_DMA_BUF_ALIGN         4       /* 4-byte alignment */

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static SDMMC_HandleTypeDef hsdmmc;
static DMA_HandleTypeDef hdma_sdmmc_tx;
static DMA_HandleTypeDef hdma_sdmmc_rx;

static sdmmc_state_t s_state = SDMMC_STATE_IDLE;
static sdmmc_stats_t s_stats = {0};

static sdmmc_tx_complete_cb_t s_tx_cb = NULL;
static sdmmc_rx_complete_cb_t s_rx_cb = NULL;
static sdmmc_error_cb_t s_err_cb = NULL;

static volatile uint8_t s_dma_tx_done = 0;
static volatile uint8_t s_dma_rx_done = 0;
static volatile uint8_t s_initialized = 0;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void sdmmc_gpio_init(void);
static void sdmmc_clock_init(void);
static sdmmc_err_t sdmmc_dma_init(void);
static sdmmc_err_t sdmmc_periph_init(void);
static void sdmmc_irq_init(void);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 SDMMC1 SDIO Host 接口
 */
sdmmc_err_t sdmmc_driver_init(void)
{
    HAL_StatusTypeDef status;

    if (s_initialized) {
        return SDMMC_OK;
    }

    /* 时钟初始化 */
    sdmmc_clock_init();

    /* GPIO 初始化 */
    sdmmc_gpio_init();

    /* 外设初始化 */
    sdmmc_err_t err = sdmmc_periph_init();
    if (err != SDMMC_OK) {
        return err;
    }

    /* DMA 初始化 */
    err = sdmmc_dma_init();
    if (err != SDMMC_OK) {
        return err;
    }

    /* 中断初始化 */
    sdmmc_irq_init();

    /* 配置总线宽度 */
    status = HAL_SD_ConfigWideBusOperation(&hsdmmc, SDMMC_BUS_WIDTH);
    if (status != HAL_OK) {
        s_stats.tx_errors++;
        return SDMMC_ERR_HARDWARE;
    }

    s_state = SDMMC_STATE_READY;
    s_initialized = 1;

    DBG_PRINT("SDMMC1 init: 4-bit mode, %lu Hz", SDIO_CLK_FREQ);
    return SDMMC_OK;
}

/**
 * @brief  反初始化 SDMMC1
 */
sdmmc_err_t sdmmc_driver_deinit(void)
{
    if (!s_initialized) {
        return SDMMC_OK;
    }

    /* 停止 DMA */
    HAL_DMA_Abort(&hdma_sdmmc_tx);
    HAL_DMA_Abort(&hdma_sdmmc_rx);

    /* 反初始化 SDMMC */
    HAL_SD_DeInit(&hsdmmc);

    /* 关闭时钟 */
    __HAL_RCC_SDMMC1_CLK_DISABLE();

    s_state = SDMMC_STATE_IDLE;
    s_initialized = 0;

    return SDMMC_OK;
}

/**
 * @brief  发送 SDIO 命令
 */
sdmmc_err_t sdmmc_send_command(sdmmc_cmd_t *cmd)
{
    if (!s_initialized || cmd == NULL) {
        return SDMMC_ERR_NOT_INIT;
    }

    HAL_StatusTypeDef status;
    SDMMC_CmdInitTypeDef cmd_init = {0};

    cmd_init.Argument = cmd->argument;
    cmd_init.CmdIndex = cmd->cmd_index;
    cmd_init.CPSM = SDMMC_CPSM_ENABLE;
    cmd_init.Response = SDMMC_RESPONSE_SHORT;
    cmd_init.WaitForInterrupt = SDMMC_WAIT_NO;

    status = HAL_SD_SendCommand(&hsdmmc, &cmd_init, SDMMC_CMD_TIMEOUT_MS);
    if (status != HAL_OK) {
        s_stats.timeout_errors++;
        return SDMMC_ERR_TIMEOUT;
    }

    /* 读取响应 */
    cmd->response[0] = HAL_SD_GetResponse(&hsdmmc);

    return SDMMC_OK;
}

/**
 * @brief  发送数据块 (DMA)
 */
sdmmc_err_t sdmmc_send_data_dma(sdmmc_data_t *data, uint32_t timeout_ms)
{
    if (!s_initialized || data == NULL || data->buffer == NULL) {
        return SDMMC_ERR_INVALID_PARAM;
    }

    if (s_state != SDMMC_STATE_READY) {
        return SDMMC_ERR_BUSY;
    }

    HAL_StatusTypeDef status;
    s_state = SDMMC_STATE_TX;
    s_dma_tx_done = 0;

    /* 确保数据缓冲区对齐 */
    if (((uint32_t)data->buffer % SDMMC_DMA_BUF_ALIGN) != 0) {
        s_state = SDMMC_STATE_READY;
        return SDMMC_ERR_INVALID_PARAM;
    }

    /* DMA 发送 */
    status = HAL_SD_WriteBlocks_DMA(
        &hsdmmc,
        data->buffer,
        data->block_addr,
        data->block_count
    );

    if (status != HAL_OK) {
        s_state = SDMMC_STATE_ERROR;
        s_stats.tx_errors++;
        return SDMMC_ERR_DMA;
    }

    /* 等待 DMA 完成 */
    uint32_t tick_start = HAL_GetTick();
    while (!s_dma_tx_done) {
        if ((HAL_GetTick() - tick_start) > timeout_ms) {
            HAL_DMA_Abort(&hdma_sdmmc_tx);
            s_state = SDMMC_STATE_ERROR;
            s_stats.timeout_errors++;
            return SDMMC_ERR_TIMEOUT;
        }
    }

    s_stats.tx_blocks += data->block_count;
    s_state = SDMMC_STATE_READY;

    return SDMMC_OK;
}

/**
 * @brief  接收数据块 (DMA)
 */
sdmmc_err_t sdmmc_recv_data_dma(sdmmc_data_t *data, uint32_t timeout_ms)
{
    if (!s_initialized || data == NULL || data->buffer == NULL) {
        return SDMMC_ERR_INVALID_PARAM;
    }

    if (s_state != SDMMC_STATE_READY) {
        return SDMMC_ERR_BUSY;
    }

    HAL_StatusTypeDef status;
    s_state = SDMMC_STATE_RX;
    s_dma_rx_done = 0;

    /* 确保数据缓冲区对齐 */
    if (((uint32_t)data->buffer % SDMMC_DMA_BUF_ALIGN) != 0) {
        s_state = SDMMC_STATE_READY;
        return SDMMC_ERR_INVALID_PARAM;
    }

    /* DMA 接收 */
    status = HAL_SD_ReadBlocks_DMA(
        &hsdmmc,
        data->buffer,
        data->block_addr,
        data->block_count
    );

    if (status != HAL_OK) {
        s_state = SDMMC_STATE_ERROR;
        s_stats.rx_errors++;
        return SDMMC_ERR_DMA;
    }

    /* 等待 DMA 完成 */
    uint32_t tick_start = HAL_GetTick();
    while (!s_dma_rx_done) {
        if ((HAL_GetTick() - tick_start) > timeout_ms) {
            HAL_DMA_Abort(&hdma_sdmmc_rx);
            s_state = SDMMC_STATE_ERROR;
            s_stats.timeout_errors++;
            return SDMMC_ERR_TIMEOUT;
        }
    }

    s_stats.rx_blocks += data->block_count;
    s_state = SDMMC_STATE_READY;

    return SDMMC_OK;
}

/**
 * @brief  设置 SDIO 总线宽度
 */
sdmmc_err_t sdmmc_set_bus_width(uint8_t width)
{
    if (!s_initialized) {
        return SDMMC_ERR_NOT_INIT;
    }

    HAL_StatusTypeDef status;
    uint32_t bus_width;

    switch (width) {
        case 1:
            bus_width = SDMMC_BUS_WIDE_1B;
            break;
        case 4:
            bus_width = SDMMC_BUS_WIDE_4B;
            break;
        case 8:
            bus_width = SDMMC_BUS_WIDE_8B;
            break;
        default:
            return SDMMC_ERR_INVALID_PARAM;
    }

    status = HAL_SD_ConfigWideBusOperation(&hsdmmc, bus_width);
    if (status != HAL_OK) {
        return SDMMC_ERR_HARDWARE;
    }

    return SDMMC_OK;
}

/**
 * @brief  设置 SDIO 时钟频率
 */
sdmmc_err_t sdmmc_set_clock(uint32_t freq_hz)
{
    if (!s_initialized) {
        return SDMMC_ERR_NOT_INIT;
    }

    /* 计算分频系数: SDMMC_CK = SDMMCCLK / (2 * ClockDiv) */
    /* SDMMCCLK = 200MHz (PLL1_Q) */
    /* ClockDiv = SDMMCCLK / (2 * freq_hz) */
    uint32_t clk_div = (SDIO_CLK_FREQ / 2 + freq_hz - 1) / freq_hz;

    if (clk_div < 1) {
        clk_div = 1;
    }
    if (clk_div > 255) {
        clk_div = 255;
    }

    HAL_StatusTypeDef status = HAL_SD_ConfigClock(&hsdmmc, clk_div);
    if (status != HAL_OK) {
        return SDMMC_ERR_HARDWARE;
    }

    DBG_PRINT("SDMMC clock set: %lu Hz (div=%lu)", freq_hz, clk_div);
    return SDMMC_OK;
}

/**
 * @brief  注册传输完成回调
 */
sdmmc_err_t sdmmc_register_callbacks(
    sdmmc_tx_complete_cb_t tx_cb,
    sdmmc_rx_complete_cb_t rx_cb,
    sdmmc_error_cb_t err_cb
)
{
    s_tx_cb = tx_cb;
    s_rx_cb = rx_cb;
    s_err_cb = err_cb;
    return SDMMC_OK;
}

/**
 * @brief  获取当前状态
 */
sdmmc_state_t sdmmc_get_state(void)
{
    return s_state;
}

/**
 * @brief  获取统计信息
 */
sdmmc_err_t sdmmc_get_stats(sdmmc_stats_t *stats)
{
    if (stats == NULL) {
        return SDMMC_ERR_INVALID_PARAM;
    }

    memcpy(stats, &s_stats, sizeof(sdmmc_stats_t));
    return SDMMC_OK;
}

/**
 * @brief  SDMMC1 中断服务程序
 */
void sdmmc_driver_isr(void)
{
    HAL_SD_IRQHandler(&hsdmmc);
}

/**
 * @brief  复位 SDMMC1 外设
 */
sdmmc_err_t sdmmc_reset(void)
{
    if (!s_initialized) {
        return SDMMC_ERR_NOT_INIT;
    }

    __HAL_RCC_SDMMC1_FORCE_RESET();
    __HAL_RCC_SDMMC1_RELEASE_RESET();

    s_state = SDMMC_STATE_IDLE;
    memset(&s_stats, 0, sizeof(s_stats));

    return SDMMC_OK;
}

/* =============================================================================
 * HAL 回调函数
 * ==========================================================================*/

/**
 * @brief  SDIO TX DMA 完成回调
 */
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    s_dma_tx_done = 1;

    if (s_tx_cb != NULL) {
        s_tx_cb();
    }
}

/**
 * @brief  SDIO RX DMA 完成回调
 */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    s_dma_rx_done = 1;

    if (s_rx_cb != NULL) {
        s_rx_cb();
    }
}

/**
 * @brief  SDIO 错误回调
 */
void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    s_stats.tx_errors++;
    s_state = SDMMC_STATE_ERROR;

    if (s_err_cb != NULL) {
        s_err_cb(hsd->ErrorCode);
    }
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  SDMMC GPIO 初始化
 */
static void sdmmc_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIO 时钟 */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PC8-PC12: D0-D3, CLK, CMD */
    GPIO_InitStruct.Pin = SDIO_D0_PIN | SDIO_D1_PIN | SDIO_D2_PIN |
                          SDIO_CLK_PIN | SDIO_CMD_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = SDIO_GPIO_AF;
    HAL_GPIO_Init(SDIO_GPIO_PORT_CLK_CMD_D02, &GPIO_InitStruct);

    /* PD2: D3 */
    GPIO_InitStruct.Pin = SDIO_D3_PIN;
    HAL_GPIO_Init(SDIO_GPIO_PORT_D3, &GPIO_InitStruct);
}

/**
 * @brief  SDMMC 时钟初始化
 */
static void sdmmc_clock_init(void)
{
    __HAL_RCC_SDMMC1_CLK_ENABLE();
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __HAL_RCC_SDMMC1_RELEASE_RESET();
}

/**
 * @brief  SDMMC 外设初始化
 */
static sdmmc_err_t sdmmc_periph_init(void)
{
    HAL_StatusTypeDef status;

    hsdmmc.Instance = SDMMC1;
    hsdmmc.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsdmmc.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsdmmc.Init.BusWide = (SDMMC_BUS_WIDTH == 4) ? SDMMC_BUS_WIDE_4B : SDMMC_BUS_WIDE_1B;
    hsdmmc.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsdmmc.Init.ClockDiv = SDMMC_CLK_DIV;

    status = HAL_SD_Init(&hsdmmc);
    if (status != HAL_OK) {
        return SDMMC_ERR_HARDWARE;
    }

    return SDMMC_OK;
}

/**
 * @brief  SDMMC DMA 初始化
 */
static sdmmc_err_t sdmmc_dma_init(void)
{
    HAL_StatusTypeDef status;

    /* 使能 DMA1 时钟 */
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* TX DMA */
    hdma_sdmmc_tx.Instance = SDMMC_DMA_TX_STREAM;
    hdma_sdmmc_tx.Init.Request = SDMMC_DMA_TX_CHANNEL;
    hdma_sdmmc_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_sdmmc_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sdmmc_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sdmmc_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sdmmc_tx.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sdmmc_tx.Init.Mode = DMA_NORMAL;
    hdma_sdmmc_tx.Init.Priority = DMA_PRIORITY_VERY_HIGH;

    status = HAL_DMA_Init(&hdma_sdmmc_tx);
    if (status != HAL_OK) {
        return SDMMC_ERR_DMA;
    }

    __HAL_LINKDMA(&hsdmmc, hdmatx, hdma_sdmmc_tx);

    /* RX DMA */
    hdma_sdmmc_rx.Instance = SDMMC_DMA_RX_STREAM;
    hdma_sdmmc_rx.Init.Request = SDMMC_DMA_RX_CHANNEL;
    hdma_sdmmc_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_sdmmc_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sdmmc_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sdmmc_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sdmmc_rx.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sdmmc_rx.Init.Mode = DMA_NORMAL;
    hdma_sdmmc_rx.Init.Priority = DMA_PRIORITY_VERY_HIGH;

    status = HAL_DMA_Init(&hdma_sdmmc_rx);
    if (status != HAL_OK) {
        return SDMMC_ERR_DMA;
    }

    __HAL_LINKDMA(&hsdmmc, hdmarx, hdma_sdmmc_rx);

    return SDMMC_OK;
}

/**
 * @brief  SDMMC 中断初始化
 */
static void sdmmc_irq_init(void)
{
    /* SDMMC1 全局中断 */
    HAL_NVIC_SetPriority(SDMMC1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(SDMMC1_IRQn);

    /* DMA TX 中断 */
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

    /* DMA RX 中断 */
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
}
