/**
  ******************************************************************************
  * @file    fmc_sdram.c
  * @brief   FMC SDRAM 驱动实现 (STM32N657L0H3Q) — P0批次
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - IS42S32160F: 32M x 16bit x 4banks = 256Mbit = 32MB per chip
  *   本项目使用 32-bit 数据宽度 (2片并联), 总容量 64MB
  * - 初始化序列: Precharge All -> Auto Refresh x2 -> Load Mode Register
  * - 166MHz 时钟, CL=3
  ******************************************************************************
  */

#include "fmc_sdram.h"
#include "rcc_config.h"
#include "gpio_uart.h"
#include "stm32n6xx_hal.h"

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static SDRAM_HandleTypeDef hsdram;
static FMC_SDRAM_TimingTypeDef SDRAM_Timing;
static FMC_SDRAM_CommandTypeDef SDRAM_Command;

static fmc_sdram_state_t s_state = FMC_SDRAM_STATE_IDLE;
static fmc_sdram_info_t s_info = {
    .base_addr = SDRAM_DEVICE_ADDR,
    .size = SDRAM_DEVICE_SIZE,
    .width = 32,
    .freq_hz = FMC_CLK_FREQ,
    .cas_latency = 3,
    .refresh_count = SDRAM_REFRESH_COUNT,
};

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void fmc_sdram_gpio_init(void);
static void fmc_sdram_clock_init(void);
static fmc_sdram_err_t fmc_sdram_periph_init(void);
static fmc_sdram_err_t fmc_sdram_init_sequence(void);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 FMC SDRAM
 */
fmc_sdram_err_t fmc_sdram_init(void)
{
    HAL_StatusTypeDef status;

    if (s_state != FMC_SDRAM_STATE_IDLE) {
        return FMC_SDRAM_ERR_NOT_INIT;
    }

    /* 时钟初始化 */
    fmc_sdram_clock_init();

    /* GPIO 初始化 */
    fmc_sdram_gpio_init();

    /* 外设初始化 */
    fmc_sdram_err_t err = fmc_sdram_periph_init();
    if (err != FMC_SDRAM_OK) {
        s_state = FMC_SDRAM_STATE_ERROR;
        return err;
    }

    /* 执行 SDRAM 初始化序列 */
    err = fmc_sdram_init_sequence();
    if (err != FMC_SDRAM_OK) {
        s_state = FMC_SDRAM_STATE_ERROR;
        return err;
    }

    s_state = FMC_SDRAM_STATE_READY;

    DBG_PRINT("SDRAM init: 64MB @ 0x%08X, %lu MHz, CL=%lu",
              SDRAM_DEVICE_ADDR, FMC_CLK_FREQ / 1000000, s_info.cas_latency);
    return FMC_SDRAM_OK;
}

/**
 * @brief  反初始化 FMC SDRAM
 */
fmc_sdram_err_t fmc_sdram_deinit(void)
{
    if (s_state == FMC_SDRAM_STATE_IDLE) {
        return FMC_SDRAM_OK;
    }

    /* 进入自刷新模式 */
    if (s_state == FMC_SDRAM_STATE_READY) {
        fmc_sdram_enter_self_refresh();
    }

    /* 反初始化 FMC */
    HAL_SDRAM_DeInit(&hsdram);

    /* 关闭时钟 */
    __HAL_RCC_FMC_CLK_DISABLE();

    s_state = FMC_SDRAM_STATE_IDLE;
    return FMC_SDRAM_OK;
}

/**
 * @brief  SDRAM 内存测试
 */
fmc_sdram_err_t fmc_sdram_test(void)
{
    if (s_state != FMC_SDRAM_STATE_READY) {
        return FMC_SDRAM_ERR_NOT_INIT;
    }

    volatile uint32_t *p_addr = (volatile uint32_t *)SDRAM_DEVICE_ADDR;
    const uint32_t test_size = 0x100000; /* 测试 1MB */
    uint32_t i;

    DBG_PRINT("SDRAM test start: 1MB sample @ 0x%08X", SDRAM_DEVICE_ADDR);

    /* 测试 1: 写读 0x55555555 */
    DBG_PRINT("SDRAM test 1: 0x55555555 pattern");
    for (i = 0; i < test_size / 4; i++) {
        p_addr[i] = 0x55555555U;
    }
    for (i = 0; i < test_size / 4; i++) {
        if (p_addr[i] != 0x55555555U) {
            ERR_PRINT("SDRAM test fail @ 0x%08X: exp=0x55555555, got=0x%08X",
                      (uint32_t)&p_addr[i], p_addr[i]);
            return FMC_SDRAM_ERR_TEST_FAIL;
        }
    }

    /* 测试 2: 写读 0xAAAAAAAA */
    DBG_PRINT("SDRAM test 2: 0xAAAAAAAA pattern");
    for (i = 0; i < test_size / 4; i++) {
        p_addr[i] = 0xAAAAAAAAU;
    }
    for (i = 0; i < test_size / 4; i++) {
        if (p_addr[i] != 0xAAAAAAAAU) {
            ERR_PRINT("SDRAM test fail @ 0x%08X: exp=0xAAAAAAAA, got=0x%08X",
                      (uint32_t)&p_addr[i], p_addr[i]);
            return FMC_SDRAM_ERR_TEST_FAIL;
        }
    }

    /* 测试 3: 地址模式 */
    DBG_PRINT("SDRAM test 3: address pattern");
    for (i = 0; i < test_size / 4; i++) {
        p_addr[i] = (uint32_t)&p_addr[i];
    }
    for (i = 0; i < test_size / 4; i++) {
        if (p_addr[i] != (uint32_t)&p_addr[i]) {
            ERR_PRINT("SDRAM test fail @ 0x%08X: address pattern mismatch",
                      (uint32_t)&p_addr[i]);
            return FMC_SDRAM_ERR_TEST_FAIL;
        }
    }

    /* 测试 4: 行走位 (walking ones) */
    DBG_PRINT("SDRAM test 4: walking ones");
    for (uint32_t bit = 0; bit < 32; bit++) {
        uint32_t pattern = (1U << bit);
        for (i = 0; i < 256; i++) {
            p_addr[i] = pattern;
        }
        for (i = 0; i < 256; i++) {
            if (p_addr[i] != pattern) {
                ERR_PRINT("SDRAM walking-bit fail: bit %lu", bit);
                return FMC_SDRAM_ERR_TEST_FAIL;
            }
        }
    }

    /* 测试 5: 行走零 (walking zeros) */
    DBG_PRINT("SDRAM test 5: walking zeros");
    for (uint32_t bit = 0; bit < 32; bit++) {
        uint32_t pattern = ~(1U << bit);
        for (i = 0; i < 256; i++) {
            p_addr[i] = pattern;
        }
        for (i = 0; i < 256; i++) {
            if (p_addr[i] != pattern) {
                ERR_PRINT("SDRAM walking-zero fail: bit %lu", bit);
                return FMC_SDRAM_ERR_TEST_FAIL;
            }
        }
    }

    /* 测试 6: 伪随机序列 */
    DBG_PRINT("SDRAM test 6: pseudo-random");
    uint32_t lfsr = 0xACE1u;
    for (i = 0; i < test_size / 4; i++) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
        p_addr[i] = lfsr;
    }
    lfsr = 0xACE1u;
    for (i = 0; i < test_size / 4; i++) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
        if (p_addr[i] != lfsr) {
            ERR_PRINT("SDRAM random test fail @ 0x%08X", (uint32_t)&p_addr[i]);
            return FMC_SDRAM_ERR_TEST_FAIL;
        }
    }

    DBG_PRINT("SDRAM test passed (1MB sample, 6 patterns)");
    return FMC_SDRAM_OK;
}

/**
 * @brief  获取 SDRAM 基地址
 */
uint32_t fmc_sdram_get_base_addr(void)
{
    return SDRAM_DEVICE_ADDR;
}

/**
 * @brief  获取 SDRAM 容量
 */
uint32_t fmc_sdram_get_size(void)
{
    return SDRAM_DEVICE_SIZE;
}

/**
 * @brief  获取 SDRAM 信息
 */
fmc_sdram_err_t fmc_sdram_get_info(fmc_sdram_info_t *info)
{
    if (info == NULL) {
        return FMC_SDRAM_ERR_INVALID_PARAM;
    }

    memcpy(info, &s_info, sizeof(fmc_sdram_info_t));
    return FMC_SDRAM_OK;
}

/**
 * @brief  进入自刷新模式 (低功耗)
 */
fmc_sdram_err_t fmc_sdram_enter_self_refresh(void)
{
    if (s_state != FMC_SDRAM_STATE_READY) {
        return FMC_SDRAM_ERR_NOT_INIT;
    }

    HAL_StatusTypeDef status;

    SDRAM_Command.CommandMode = FMC_SDRAM_CMD_SELFREFRESH_MODE;
    SDRAM_Command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    SDRAM_Command.AutoRefreshNumber = 1;
    SDRAM_Command.ModeRegisterDefinition = 0;

    status = HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT);
    if (status != HAL_OK) {
        return FMC_SDRAM_ERR_HARDWARE;
    }

    s_state = FMC_SDRAM_STATE_SELF_REFRESH;
    DBG_PRINT("SDRAM entered self-refresh mode");
    return FMC_SDRAM_OK;
}

/**
 * @brief  退出自刷新模式
 */
fmc_sdram_err_t fmc_sdram_exit_self_refresh(void)
{
    if (s_state != FMC_SDRAM_STATE_SELF_REFRESH) {
        return FMC_SDRAM_ERR_NOT_INIT;
    }

    HAL_StatusTypeDef status;

    /* 发送自刷新退出命令 */
    SDRAM_Command.CommandMode = FMC_SDRAM_CMD_NORMAL_MODE;
    SDRAM_Command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    SDRAM_Command.AutoRefreshNumber = 1;
    SDRAM_Command.ModeRegisterDefinition = 0;

    status = HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT);
    if (status != HAL_OK) {
        return FMC_SDRAM_ERR_HARDWARE;
    }

    /* 等待 tXSR (72ns @ 166MHz = 8 clocks) */
    for (volatile uint32_t i = 0; i < 20; i++) {
        __NOP();
    }

    s_state = FMC_SDRAM_STATE_READY;
    DBG_PRINT("SDRAM exited self-refresh mode");
    return FMC_SDRAM_OK;
}

/**
 * @brief  写入 SDRAM
 */
fmc_sdram_err_t fmc_sdram_write(uint32_t addr, const uint32_t *data, uint32_t len)
{
    if (s_state != FMC_SDRAM_STATE_READY) {
        return FMC_SDRAM_ERR_NOT_INIT;
    }

    if (data == NULL || len == 0) {
        return FMC_SDRAM_ERR_INVALID_PARAM;
    }

    if (addr + len > SDRAM_DEVICE_SIZE) {
        return FMC_SDRAM_ERR_INVALID_PARAM;
    }

    volatile uint32_t *dest = (volatile uint32_t *)(SDRAM_DEVICE_ADDR + addr);
    uint32_t words = len / 4;

    for (uint32_t i = 0; i < words; i++) {
        dest[i] = data[i];
    }

    /* 数据同步屏障 */
    __DSB();

    return FMC_SDRAM_OK;
}

/**
 * @brief  读取 SDRAM
 */
fmc_sdram_err_t fmc_sdram_read(uint32_t addr, uint32_t *data, uint32_t len)
{
    if (s_state != FMC_SDRAM_STATE_READY) {
        return FMC_SDRAM_ERR_NOT_INIT;
    }

    if (data == NULL || len == 0) {
        return FMC_SDRAM_ERR_INVALID_PARAM;
    }

    if (addr + len > SDRAM_DEVICE_SIZE) {
        return FMC_SDRAM_ERR_INVALID_PARAM;
    }

    volatile uint32_t *src = (volatile uint32_t *)(SDRAM_DEVICE_ADDR + addr);
    uint32_t words = len / 4;

    for (uint32_t i = 0; i < words; i++) {
        data[i] = src[i];
    }

    return FMC_SDRAM_OK;
}

/**
 * @brief  获取当前状态
 */
fmc_sdram_state_t fmc_sdram_get_state(void)
{
    return s_state;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  SDRAM GPIO 初始化
 */
static void fmc_sdram_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIO 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* 数据总线 GPIO 配置 */
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

    /* PD0-PD1, PD8-PD10, PD14-PD15 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 |
                          GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* PE7-PE15 */
    GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                          GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                          GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* 地址总线 GPIO 配置 */
    /* PF0-PF5, PF12-PF15 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
                          GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
                          GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    /* PG0-PG5 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
                          GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* 控制信号 GPIO 配置 */
    /* PC0: SDNWE */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PF11: SDNRAS */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    /* PG15: SDNCAS */
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* PB5: SDCKE1 */
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PG8: SDNE1 */
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* 字节使能信号 (NBL0-NBL3) */
    /* 通常由 FMC 控制器自动管理, 但某些引脚需要配置 */
    /* PE0-PE1: NBL0-NBL1 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* PH2-PH3: SDCKE0/SDNE0 (备用) */
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);
}

/**
 * @brief  SDRAM 时钟初始化
 */
static void fmc_sdram_clock_init(void)
{
    /* 使能 FMC 时钟 */
    __HAL_RCC_FMC_CLK_ENABLE();
    __HAL_RCC_FMC_FORCE_RESET();
    __HAL_RCC_FMC_RELEASE_RESET();
}

/**
 * @brief  SDRAM 外设初始化
 */
static fmc_sdram_err_t fmc_sdram_periph_init(void)
{
    HAL_StatusTypeDef status;

    hsdram.Instance = FMC_SDRAM_DEVICE;

    /* SDRAM 时序配置 */
    SDRAM_Timing.LoadToActiveDelay = SDRAM_TMRD;
    SDRAM_Timing.ExitSelfRefreshDelay = SDRAM_TXSR;
    SDRAM_Timing.SelfRefreshTime = SDRAM_TRAS;
    SDRAM_Timing.RowCycleDelay = SDRAM_TRC;
    SDRAM_Timing.WriteRecoveryTime = SDRAM_TWR;
    SDRAM_Timing.RPDelay = SDRAM_TRP;
    SDRAM_Timing.RCDDelay = SDRAM_TRCD;

    /* SDRAM 控制器配置 */
    FMC_SDRAM_InitTypeDef SDRAM_InitStruct = {0};
    SDRAM_InitStruct.SDBank = FMC_SDRAM_BANK1;
    SDRAM_InitStruct.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_10;
    SDRAM_InitStruct.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
    SDRAM_InitStruct.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_32;
    SDRAM_InitStruct.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    SDRAM_InitStruct.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    SDRAM_InitStruct.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    SDRAM_InitStruct.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    SDRAM_InitStruct.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    SDRAM_InitStruct.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

    status = HAL_SDRAM_Init(&hsdram, &SDRAM_InitStruct, &SDRAM_Timing);
    if (status != HAL_OK) {
        return FMC_SDRAM_ERR_HARDWARE;
    }

    return FMC_SDRAM_OK;
}

/**
 * @brief  SDRAM 初始化序列
 * @note   1. Bank Enable -> 2. Precharge All -> 3. Auto Refresh x2 -> 4. Load Mode Register
 */
static fmc_sdram_err_t fmc_sdram_init_sequence(void)
{
    HAL_StatusTypeDef status;
    uint32_t tmpmrd;

    /* Step 1: Bank Enable (Clock Enable) */
    SDRAM_Command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    SDRAM_Command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    SDRAM_Command.AutoRefreshNumber = 1;
    SDRAM_Command.ModeRegisterDefinition = 0;

    status = HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT);
    if (status != HAL_OK) {
        return FMC_SDRAM_ERR_HARDWARE;
    }

    /* 等待稳定 (至少 100us, 但 HAL_Delay(1) 更安全) */
    HAL_Delay(1);

    /* Step 2: Precharge All */
    SDRAM_Command.CommandMode = FMC_SDRAM_CMD_PALL;
    status = HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT);
    if (status != HAL_OK) {
        return FMC_SDRAM_ERR_HARDWARE;
    }

    /* Step 3: Auto Refresh x2 */
    SDRAM_Command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    SDRAM_Command.AutoRefreshNumber = 2;

    for (uint8_t i = 0; i < 2; i++) {
        status = HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT);
        if (status != HAL_OK) {
            return FMC_SDRAM_ERR_HARDWARE;
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
             SDRAM_MODEREG_BURST_TYPE_SEQ |
             SDRAM_MODEREG_CAS_LATENCY_3 |
             SDRAM_MODEREG_OPERATING_MODE_STD |
             SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    SDRAM_Command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    SDRAM_Command.AutoRefreshNumber = 1;
    SDRAM_Command.ModeRegisterDefinition = tmpmrd;

    status = HAL_SDRAM_SendCommand(&hsdram, &SDRAM_Command, SDRAM_TIMEOUT);
    if (status != HAL_OK) {
        return FMC_SDRAM_ERR_HARDWARE;
    }

    /* Step 5: 设置刷新计数器 */
    status = HAL_SDRAM_ProgramRefreshRate(&hsdram, SDRAM_REFRESH_COUNT);
    if (status != HAL_OK) {
        return FMC_SDRAM_ERR_HARDWARE;
    }

    DBG_PRINT("SDRAM init sequence complete: MRD=0x%04X, Refresh=%lu",
              tmpmrd, SDRAM_REFRESH_COUNT);
    return FMC_SDRAM_OK;
}
