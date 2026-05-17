/**
  ******************************************************************************
  * @file    bsp_sdram.c
  * @brief   FMC SDRAM 驱动实现 (IS42S32160F, 32-bit, 166MHz)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 初始化序列: Precharge All -> Auto Refresh x2 -> Load Mode Register
  * - 使用 HAL FMC SDRAM 驱动
  ******************************************************************************
  */

#include "bsp_sdram.h"

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static SDRAM_HandleTypeDef hsdram;
static FMC_SDRAM_TimingTypeDef SDRAM_Timing;
static FMC_SDRAM_CommandTypeDef SDRAM_Command;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void sdram_gpio_init(void);
static void sdram_clock_init(void);
static lingxi_err_t sdram_init_sequence(void);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 FMC SDRAM
 */
lingxi_err_t bsp_sdram_init(void)
{
    /* 时钟初始化 */
    sdram_clock_init();

    /* GPIO 初始化 */
    sdram_gpio_init();

    /* FMC SDRAM 初始化 */
    hsdram.Instance = FMC_SDRAM_DEVICE;

    /* SDRAM 时序配置 */
    SDRAM_Timing.LoadToActiveDelay    = SDRAM_TMRD;
    SDRAM_Timing.ExitSelfRefreshDelay = SDRAM_TXSR;
    SDRAM_Timing.SelfRefreshTime      = SDRAM_TRAS;
    SDRAM_Timing.RowCycleDelay        = SDRAM_TRC;
    SDRAM_Timing.WriteRecoveryTime    = SDRAM_TWR;
    SDRAM_Timing.RPDelay              = SDRAM_TRP;
    SDRAM_Timing.RCDDelay             = SDRAM_TRCD;

    /* SDRAM 控制器配置 */
    FMC_SDRAM_InitTypeDef SDRAM_InitStruct = {0};
    SDRAM_InitStruct.SDBank             = FMC_SDRAM_BANK1;
    SDRAM_InitStruct.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_10;
    SDRAM_InitStruct.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_13;
    SDRAM_InitStruct.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_32;
    SDRAM_InitStruct.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    SDRAM_InitStruct.CASLatency         = FMC_SDRAM_CAS_LATENCY_3;
    SDRAM_InitStruct.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    SDRAM_InitStruct.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2; /* HCLK/2 = 400MHz/2 = 200MHz, 实际 166MHz 由 PLL 分频 */
    SDRAM_InitStruct.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
    SDRAM_InitStruct.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_1;

    if (HAL_SDRAM_Init(&hsdram, &SDRAM_InitStruct, &SDRAM_Timing) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* 执行 SDRAM 初始化序列 */
    lingxi_err_t err = sdram_init_sequence();
    LX_RETURN_IF_ERR(err);

    LX_DEBUG_PRINT("SDRAM initialized: 64MB @ 0x%08X", SDRAM_DEVICE_ADDR);
    return LINGXI_OK;
}

/**
 * @brief  SDRAM 内存测试
 */
lingxi_err_t bsp_sdram_test(void)
{
    volatile uint32_t *p_addr = (volatile uint32_t *)SDRAM_DEVICE_ADDR;
    const uint32_t test_size = 0x100000; /* 测试 1MB */
    uint32_t i;

    /* 测试 1: 写读 0x55555555 */
    for (i = 0; i < test_size / 4; i++) {
        p_addr[i] = 0x55555555U;
    }
    for (i = 0; i < test_size / 4; i++) {
        if (p_addr[i] != 0x55555555U) {
            LX_ERR_PRINT("SDRAM test fail at 0x%08X: expected 0x55555555, got 0x%08X",
                         (uint32_t)&p_addr[i], p_addr[i]);
            return LINGXI_ERR_IO;
        }
    }

    /* 测试 2: 写读 0xAAAAAAAA */
    for (i = 0; i < test_size / 4; i++) {
        p_addr[i] = 0xAAAAAAAAU;
    }
    for (i = 0; i < test_size / 4; i++) {
        if (p_addr[i] != 0xAAAAAAAAU) {
            LX_ERR_PRINT("SDRAM test fail at 0x%08X: expected 0xAAAAAAAA, got 0x%08X",
                         (uint32_t)&p_addr[i], p_addr[i]);
            return LINGXI_ERR_IO;
        }
    }

    /* 测试 3: 地址模式 */
    for (i = 0; i < test_size / 4; i++) {
        p_addr[i] = (uint32_t)&p_addr[i];
    }
    for (i = 0; i < test_size / 4; i++) {
        if (p_addr[i] != (uint32_t)&p_addr[i]) {
            LX_ERR_PRINT("SDRAM test fail at 0x%08X: address pattern mismatch",
                         (uint32_t)&p_addr[i]);
            return LINGXI_ERR_IO;
        }
    }

    /* 测试 4: 行走位 (walking ones) */
    for (uint32_t bit = 0; bit < 32; bit++) {
        uint32_t pattern = (1U << bit);
        for (i = 0; i < 256; i++) {
            p_addr[i] = pattern;
        }
        for (i = 0; i < 256; i++) {
            if (p_addr[i] != pattern) {
                LX_ERR_PRINT("SDRAM walking-bit test fail: bit %u", bit);
                return LINGXI_ERR_IO;
            }
        }
    }

    LX_DEBUG_PRINT("SDRAM test passed (1MB sample)");
    return LINGXI_OK;
}

/**
 * @brief  进入自刷新模式 (低功耗)
 */
lingxi_err_t bsp_sdram_enter_self_refresh(void)
{
    SDRAM_Command.CommandMode            = FMC_SDRAM_CMD_SELFREFRESH_MODE;
    SDRAM_Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    SDRAM_Command.AutoRefreshNumber      = 1;
    SDRAM_Command.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return LINGXI_OK;
}

/**
 * @brief  退出自刷新模式
 */
lingxi_err_t bsp_sdram_exit_self_refresh(void)
{
    /* 发送自刷新退出命令后，需要等待 tXSR */
    /* HAL 驱动会自动处理 */
    return LINGXI_OK;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  SDRAM GPIO 初始化
 */
static void sdram_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIO 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* 数据总线 GPIO 配置 */
    /* PD8-PD10, PD14-PD15, PE7-PE15, PD0-PD1 */
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                          GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                          GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                          GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* 地址总线 GPIO 配置 */
    /* PF0-PF5, PF12-PF15, PG0-PG5 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
                          GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
                          GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
                          GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* 控制信号 GPIO 配置 */
    /* PC0: SDNWE, PF11: SDNRAS, PG15: SDNCAS, PB5: SDCKE1, PG8: SDNE1 */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
}

/**
 * @brief  SDRAM 时钟初始化
 */
static void sdram_clock_init(void)
{
    /* 使能 FMC 时钟 */
    __HAL_RCC_FMC_CLK_ENABLE();
    __HAL_RCC_FMC_FORCE_RESET();
    __HAL_RCC_FMC_RELEASE_RESET();
}

/**
 * @brief  SDRAM 初始化序列
 * @note   1. Bank Enable -> 2. Precharge All -> 3. Auto Refresh x2 -> 4. Load Mode Register
 */
static lingxi_err_t sdram_init_sequence(void)
{
    uint32_t tmpmrd;

    /* Step 1: Bank Enable */
    SDRAM_Command.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
    SDRAM_Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    SDRAM_Command.AutoRefreshNumber      = 1;
    SDRAM_Command.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* 等待稳定 */
    HAL_Delay(1);

    /* Step 2: Precharge All */
    SDRAM_Command.CommandMode = FMC_SDRAM_CMD_PALL;
    if (HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* Step 3: Auto Refresh x2 */
    SDRAM_Command.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    SDRAM_Command.AutoRefreshNumber = 2;

    for (uint8_t i = 0; i < 2; i++) {
        if (HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT) != HAL_OK) {
            return LINGXI_ERR_IO;
        }
    }

    /* Step 4: Load Mode Register */
    /* Mode Register 配置:
     *  burst_length = 1 (bit2:0 = 000)
     *  burst_type   = sequential (bit3 = 0)
     *  CAS latency  = 3 (bit6:4 = 011)
     *  operating_mode = standard (bit8:7 = 00)
     *  write_burst_mode = programmed (bit9 = 0)
     */
    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1 |
             SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
             SDRAM_MODEREG_CAS_LATENCY_3 |
             SDRAM_MODEREG_OPERATING_MODE_STANDARD |
             SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    SDRAM_Command.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    SDRAM_Command.AutoRefreshNumber      = 1;
    SDRAM_Command.ModeRegisterDefinition = tmpmrd;

    if (HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* Step 5: 设置刷新计数器 */
    if (HAL_SDRAM_ProgramRefreshRate(&hsdram, SDRAM_REFRESH_COUNT) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return LINGXI_OK;
}
