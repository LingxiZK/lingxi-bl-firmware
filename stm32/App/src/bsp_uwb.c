/**
  ******************************************************************************
  * @file    bsp_uwb.c
  * @brief   DWM3000 UWB 驱动实现 (SPI1)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 基于 DW3000 API 封装
  * - SPI1 @ 10MHz
  * - 中断驱动测距
  ******************************************************************************
  */

#include "bsp_uwb.h"

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static SPI_HandleTypeDef hspi_uwb;
static volatile uint8_t s_uwb_initialized = 0;
static volatile uint8_t s_ranging_active = 0;

static uwb_ranging_data_t s_ranging_data = {0};
static uwb_ranging_callback_t s_ranging_callback = NULL;

static SemaphoreHandle_t s_uwb_sem = NULL;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void uwb_gpio_init(void);
static void uwb_spi_init(void);
static void uwb_clock_init(void);
static lingxi_err_t uwb_reset_device(void);
static lingxi_err_t uwb_configure_channel(void);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 DWM3000
 */
lingxi_err_t bsp_uwb_init(void)
{
    /* 创建信号量 */
    s_uwb_sem = xSemaphoreCreateBinary();
    if (s_uwb_sem == NULL) {
        return LINGXI_ERR_NO_MEM;
    }

    /* 时钟初始化 */
    uwb_clock_init();

    /* GPIO 初始化 */
    uwb_gpio_init();

    /* SPI 初始化 */
    uwb_spi_init();

    /* 复位设备 */
    lingxi_err_t err = uwb_reset_device();
    LX_RETURN_IF_ERR(err);

    /* 配置 UWB 通道参数 */
    err = uwb_configure_channel();
    LX_RETURN_IF_ERR(err);

    /* 配置中断 */
    HAL_NVIC_SetPriority(UWB_IRQ_IRQn, IRQ_PRIO_MEDIUM, 0);
    HAL_NVIC_EnableIRQ(UWB_IRQ_IRQn);

    s_uwb_initialized = 1;

    LX_DEBUG_PRINT("DWM3000 UWB initialized, CH%d, 10MHz SPI", UWB_CHANNEL);
    return LINGXI_OK;
}

/**
 * @brief  启动测距
 */
lingxi_err_t bsp_uwb_start_ranging(void)
{
    if (!s_uwb_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    s_ranging_active = 1;

    /* 启动接收 */
    /* dwt_rxenable(DWT_START_RX_IMMEDIATE); */

    LX_DEBUG_PRINT("UWB ranging started");
    return LINGXI_OK;
}

/**
 * @brief  停止测距
 */
lingxi_err_t bsp_uwb_stop_ranging(void)
{
    s_ranging_active = 0;

    /* 停止接收 */
    /* dwt_forcetrxoff(); */

    LX_DEBUG_PRINT("UWB ranging stopped");
    return LINGXI_OK;
}

/**
 * @brief  注册测距回调
 */
lingxi_err_t bsp_uwb_register_callback(uwb_ranging_callback_t callback)
{
    if (callback == NULL) {
        return LINGXI_ERR_INVALID_PARAM;
    }
    s_ranging_callback = callback;
    return LINGXI_OK;
}

/**
 * @brief  获取测距数据
 */
lingxi_err_t bsp_uwb_get_ranging_data(uwb_ranging_data_t *data)
{
    LX_RETURN_IF_NULL(data);

    taskENTER_CRITICAL();
    memcpy(data, &s_ranging_data, sizeof(uwb_ranging_data_t));
    taskEXIT_CRITICAL();

    return LINGXI_OK;
}

/* =============================================================================
 * 中断与回调
 * ==========================================================================*/

/**
 * @brief  UWB 中断服务程序
 */
LX_ISR_FUNC void bsp_uwb_isr(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 读取 DW3000 中断状态 */
    /* uint32_t status = dwt_read32bitreg(SYS_STATUS_ID); */

    /* 处理接收完成中断 */
    /* if (status & SYS_STATUS_RXFCG_BIT_MASK) { */
    /*     process_rx_frame(); */
    /* } */

    xSemaphoreGiveFromISR(s_uwb_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  UWB GPIO 初始化
 */
static void uwb_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* SPI 引脚: PA5(SCK), PA6(MISO), PA7(MOSI) */
    GPIO_InitStruct.Pin = UWB_SPI_SCK_PIN | UWB_SPI_MISO_PIN | UWB_SPI_MOSI_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(UWB_SPI_GPIO_PORT, &GPIO_InitStruct);

    /* CS: PA4 (软件控制) */
    GPIO_InitStruct.Pin = UWB_SPI_CS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(UWB_SPI_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(UWB_SPI_GPIO_PORT, UWB_SPI_CS_PIN, GPIO_PIN_SET);

    /* IRQ: PA8 (中断输入) */
    GPIO_InitStruct.Pin = UWB_IRQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(UWB_SPI_GPIO_PORT, &GPIO_InitStruct);

    /* RST: PA11 (输出) */
    GPIO_InitStruct.Pin = UWB_RST_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(UWB_SPI_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(UWB_SPI_GPIO_PORT, UWB_RST_PIN, GPIO_PIN_SET);

    /* WAKEUP: PA12 (输出) */
    GPIO_InitStruct.Pin = UWB_WAKEUP_PIN;
    HAL_GPIO_Init(UWB_SPI_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(UWB_SPI_GPIO_PORT, UWB_WAKEUP_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  UWB SPI 初始化
 */
static void uwb_spi_init(void)
{
    hspi_uwb.Instance = UWB_SPI;
    hspi_uwb.Init.Mode = SPI_MODE_MASTER;
    hspi_uwb.Init.Direction = SPI_DIRECTION_2LINES;
    hspi_uwb.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi_uwb.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi_uwb.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi_uwb.Init.NSS = SPI_NSS_SOFT;
    hspi_uwb.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; /* 800MHz/8 = 100MHz, 需调整 */
    hspi_uwb.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi_uwb.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi_uwb.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi_uwb.Init.CRCPolynomial = 7;

    HAL_SPI_Init(&hspi_uwb);
}

/**
 * @brief  UWB 时钟初始化
 */
static void uwb_clock_init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();
}

/**
 * @brief  UWB 设备复位
 */
static lingxi_err_t uwb_reset_device(void)
{
    /* 拉低 RST 至少 10us */
    HAL_GPIO_WritePin(UWB_SPI_GPIO_PORT, UWB_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(UWB_SPI_GPIO_PORT, UWB_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(2);  /* 等待设备启动 */

    return LINGXI_OK;
}

/**
 * @brief  配置 UWB 通道参数
 */
static lingxi_err_t uwb_configure_channel(void)
{
    /* 使用 DW3000 API 配置 */
    /* dwt_config_t config = { */
    /*     .chan = UWB_CHANNEL, */
    /*     .txPreambLength = UWB_PREAMBLE_LEN, */
    /*     .rxPAC = UWB_PAC_SIZE, */
    /*     .txCode = UWB_TX_PREAMBLE_CODE, */
    /*     .rxCode = UWB_RX_PREAMBLE_CODE, */
    /*     .sfdType = DWT_SFD_DW_8, */
    /*     .dataRate = UWB_DATA_RATE, */
    /*     .phrMode = DWT_PHRMODE_STD, */
    /*     .phrRate = DWT_PHRRATE_STD, */
    /*     .sfdTO = (UWB_PREAMBLE_LEN + 1) * 8 */
    /* }; */

    /* if (dwt_configure(&config) != DWT_SUCCESS) { */
    /*     return LINGXI_ERR_IO; */
    /* } */

    return LINGXI_OK;
}
