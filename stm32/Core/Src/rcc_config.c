/**
  ******************************************************************************
  * @file    rcc_config.c
  * @brief   RCC 时钟配置实现 (STM32N657L0H3Q) — P0批次
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - HSE 25MHz -> PLL1 -> 800MHz SYSCLK
  * - PLL2 -> 166MHz FMC
  * - PLL3 -> 250MHz MIPI
  * - APB1/APB2/APB3: 200MHz
  ******************************************************************************
  */

#include "rcc_config.h"
#include "stm32n6xx_hal.h"

/* =============================================================================
 * 私有宏定义
 * ==========================================================================*/
#define RCC_TIMEOUT_VALUE           100U    /* 100ms timeout */
#define HSE_STARTUP_TIMEOUT         5000U   /* 5s HSE startup timeout */
#define FLASH_LATENCY_800MHZ        7       /* Flash wait states for 800MHz */

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static uint32_t s_sysclk_freq = SYSCLK_FREQ;
static uint32_t s_hclk_freq   = HCLK_FREQ;
static uint32_t s_pclk1_freq  = PCLK1_FREQ;
static uint32_t s_pclk2_freq  = PCLK2_FREQ;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static int rcc_hse_config(void);
static int rcc_pll1_config(void);
static int rcc_pll2_config(void);
static int rcc_pll3_config(void);
static int rcc_flash_latency_config(void);
static int rcc_wait_for_hse_ready(void);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  配置系统时钟 (800MHz)
 * @retval 0 成功, -1 失败
 */
int rcc_system_clock_config(void)
{
    HAL_StatusTypeDef status;

    /* Step 1: 使能 PWR 时钟并配置电压调节器 */
    __HAL_RCC_PWR_CLK_ENABLE();

    /* 配置电压调节器为高性能模式 (Scale 1) */
    status = HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
    if (status != HAL_OK) {
        return -1;
    }

    /* Step 2: 配置 Flash 延迟 */
    if (rcc_flash_latency_config() != 0) {
        return -1;
    }

    /* Step 3: 使能 HSE */
    if (rcc_hse_config() != 0) {
        return -1;
    }

    /* Step 4: 配置 PLL1 (800MHz) */
    if (rcc_pll1_config() != 0) {
        return -1;
    }

    /* Step 5: 配置 PLL2 (166MHz FMC) */
    if (rcc_pll2_config() != 0) {
        return -1;
    }

    /* Step 6: 配置 PLL3 (250MHz MIPI) */
    if (rcc_pll3_config() != 0) {
        return -1;
    }

    /* Step 7: 切换系统时钟到 PLL1 */
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                   RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                   RCC_CLOCKTYPE_PCLK3 | RCC_CLOCKTYPE_PCLK4;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;   /* HCLK = 800MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;    /* PCLK1 = 200MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV4;    /* PCLK2 = 200MHz */
    RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV4;    /* PCLK3 = 200MHz */
    RCC_ClkInitStruct.APB4CLKDivider = RCC_HCLK_DIV4;    /* PCLK4 = 200MHz */

    status = HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_800MHZ);
    if (status != HAL_OK) {
        return -1;
    }

    /* 更新 SystemCoreClock 全局变量 */
    SystemCoreClock = SYSCLK_FREQ;
    s_sysclk_freq = SYSCLK_FREQ;
    s_hclk_freq = HCLK_FREQ;
    s_pclk1_freq = PCLK1_FREQ;
    s_pclk2_freq = PCLK2_FREQ;

    return 0;
}

/**
 * @brief  使能外设时钟
 */
void rcc_periph_clock_enable(uint32_t ahb1_mask, uint32_t ahb2_mask,
                              uint32_t ahb3_mask, uint32_t apb1_mask,
                              uint32_t apb2_mask)
{
    /* AHB1 外设时钟使能 */
    if (ahb1_mask) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_GPIOE_CLK_ENABLE();
        __HAL_RCC_GPIOF_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_GPIOH_CLK_ENABLE();
        __HAL_RCC_GPIOI_CLK_ENABLE();
        __HAL_RCC_DMA1_CLK_ENABLE();
        __HAL_RCC_DMA2_CLK_ENABLE();
        __HAL_RCC_MDMA_CLK_ENABLE();
    }

    /* AHB2 外设时钟使能 */
    if (ahb2_mask & RCC_AHB2ENR_SDMMC1_EN) {
        __HAL_RCC_SDMMC1_CLK_ENABLE();
    }
    if (ahb2_mask & RCC_AHB2ENR_SDMMC2_EN) {
        __HAL_RCC_SDMMC2_CLK_ENABLE();
    }
    if (ahb2_mask & RCC_AHB2ENR_OCTOSPI1_EN) {
        __HAL_RCC_OSPI1_CLK_ENABLE();
    }
    if (ahb2_mask & RCC_AHB2ENR_OCTOSPI2_EN) {
        __HAL_RCC_OSPI2_CLK_ENABLE();
    }

    /* AHB3 外设时钟使能 */
    if (ahb3_mask & RCC_AHB3ENR_FMC_EN) {
        __HAL_RCC_FMC_CLK_ENABLE();
    }
    if (ahb3_mask & RCC_AHB3ENR_MIPI_EN) {
        /* MIPI CSI-2 时钟在专用域, 通过 RCC 特殊寄存器使能 */
        __HAL_RCC_CSI_CLK_ENABLE();
        __HAL_RCC_DCMIPP_CLK_ENABLE();
    }

    /* APB1 外设时钟使能 */
    if (apb1_mask & RCC_APB1ENR_USART1_EN) {
        __HAL_RCC_USART1_CLK_ENABLE();
    }
    if (apb1_mask & RCC_APB1ENR_USART2_EN) {
        __HAL_RCC_USART2_CLK_ENABLE();
    }
    if (apb1_mask & RCC_APB1ENR_SPI2_EN) {
        __HAL_RCC_SPI2_CLK_ENABLE();
    }
    if (apb1_mask & RCC_APB1ENR_I2C1_EN) {
        __HAL_RCC_I2C1_CLK_ENABLE();
    }
    if (apb1_mask & RCC_APB1ENR_I2C2_EN) {
        __HAL_RCC_I2C2_CLK_ENABLE();
    }

    /* APB2 外设时钟使能 */
    if (apb2_mask & RCC_APB2ENR_SPI1_EN) {
        __HAL_RCC_SPI1_CLK_ENABLE();
    }
    if (apb2_mask & RCC_APB2ENR_SPI3_EN) {
        __HAL_RCC_SPI3_CLK_ENABLE();
    }
    if (apb2_mask & RCC_APB2ENR_USART6_EN) {
        __HAL_RCC_USART6_CLK_ENABLE();
    }
}

/**
 * @brief  获取当前系统时钟频率
 */
uint32_t rcc_get_sysclk_freq(void)
{
    return s_sysclk_freq;
}

/**
 * @brief  获取当前 HCLK 频率
 */
uint32_t rcc_get_hclk_freq(void)
{
    return s_hclk_freq;
}

/**
 * @brief  获取当前 PCLK1 频率
 */
uint32_t rcc_get_pclk1_freq(void)
{
    return s_pclk1_freq;
}

/**
 * @brief  获取当前 PCLK2 频率
 */
uint32_t rcc_get_pclk2_freq(void)
{
    return s_pclk2_freq;
}

/**
 * @brief  配置 FMC 时钟 (166MHz)
 */
int rcc_fmc_clock_config(void)
{
    /* FMC 时钟源选择 PLL2 */
    /* 在 RCC 时钟配置中, FMC 通常使用 PLL2_R 输出 */
    /* 通过 RCC_D1CCIPR 或类似寄存器配置 */

    /* 确保 PLL2 已配置 */
    if ((RCC->CR & RCC_CR_PLL2RDY) == 0) {
        if (rcc_pll2_config() != 0) {
            return -1;
        }
    }

    /* 配置 FMC 内核时钟源为 PLL2 */
    /* STM32N6 系列使用 RCC_CFGR3 或 RCC_CDCCIPR */
    __HAL_RCC_FMC_CLK_ENABLE();

    return 0;
}

/**
 * @brief  配置 MIPI 时钟 (250MHz)
 */
int rcc_mipi_clock_config(void)
{
    /* MIPI CSI-2 时钟源选择 PLL3 */
    /* 确保 PLL3 已配置 */
    if ((RCC->CR & RCC_CR_PLL3RDY) == 0) {
        if (rcc_pll3_config() != 0) {
            return -1;
        }
    }

    /* 使能 MIPI CSI-2 和 DCMIPP 时钟 */
    __HAL_RCC_CSI_CLK_ENABLE();
    __HAL_RCC_DCMIPP_CLK_ENABLE();

    return 0;
}

/**
 * @brief  配置 SDIO 时钟 (25-50MHz)
 */
int rcc_sdio_clock_config(uint32_t freq_hz)
{
    (void)freq_hz;

    /* SDIO 时钟源选择 PLL1_Q (200MHz) */
    /* 通过 SDMMC 内核的分频器得到 25-50MHz */
    __HAL_RCC_SDMMC1_CLK_ENABLE();

    return 0;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  配置 HSE (25MHz 外部晶振)
 */
static int rcc_hse_config(void)
{
    HAL_StatusTypeDef status;

    /* 使能 HSE */
    status = HAL_RCC_OscConfig(&(RCC_OscInitTypeDef){
        .OscillatorType = RCC_OSCILLATORTYPE_HSE,
        .HSEState = RCC_HSE_ON,
        .PLL = {
            .PLLState = RCC_PLL_NONE,  /* 不在这里配置 PLL */
        }
    });

    if (status != HAL_OK) {
        return -1;
    }

    /* 等待 HSE 稳定 */
    return rcc_wait_for_hse_ready();
}

/**
 * @brief  等待 HSE 就绪
 */
static int rcc_wait_for_hse_ready(void)
{
    uint32_t timeout = HSE_STARTUP_TIMEOUT;

    while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY) == RESET) {
        if (timeout-- == 0) {
            return -1;  /* HSE 启动超时 */
        }
        HAL_Delay(1);
    }

    return 0;
}

/**
 * @brief  配置 PLL1 (800MHz)
 */
static int rcc_pll1_config(void)
{
    HAL_StatusTypeDef status;

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

    /* PLL1 配置: 25MHz / 5 * 160 / 1 = 800MHz */
    RCC_OscInitStruct.PLL.PLLM = PLL1_M_DIV;
    RCC_OscInitStruct.PLL.PLLN = PLL1_N_MUL;
    RCC_OscInitStruct.PLL.PLLP = PLL1_P_DIV;
    RCC_OscInitStruct.PLL.PLLQ = PLL1_Q_DIV;
    RCC_OscInitStruct.PLL.PLLR = PLL1_R_DIV;

    /* PLL1 输出使能 */
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;  /* 4-8 MHz */
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;  /* Wide VCO range */
    RCC_OscInitStruct.PLL.PLLFRACN = 0;

    status = HAL_RCC_OscConfig(&RCC_OscInitStruct);
    if (status != HAL_OK) {
        return -1;
    }

    return 0;
}

/**
 * @brief  配置 PLL2 (166MHz FMC)
 */
static int rcc_pll2_config(void)
{
    HAL_StatusTypeDef status;

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
    RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL2.PLLSource = RCC_PLLSOURCE_HSE;

    /* PLL2 配置: 25MHz / 5 * 332 / 10 = 166MHz */
    RCC_OscInitStruct.PLL2.PLLM = PLL2_M_DIV;
    RCC_OscInitStruct.PLL2.PLLN = PLL2_N_MUL;
    RCC_OscInitStruct.PLL2.PLLP = 1;
    RCC_OscInitStruct.PLL2.PLLQ = 1;
    RCC_OscInitStruct.PLL2.PLLR = PLL2_R_DIV;

    RCC_OscInitStruct.PLL2.PLLRGE = RCC_PLL2VCIRANGE_2;
    RCC_OscInitStruct.PLL2.PLLVCOSEL = RCC_PLL2VCOWIDE;
    RCC_OscInitStruct.PLL2.PLLFRACN = 0;

    status = HAL_RCC_OscConfig(&RCC_OscInitStruct);
    if (status != HAL_OK) {
        return -1;
    }

    return 0;
}

/**
 * @brief  配置 PLL3 (250MHz MIPI)
 */
static int rcc_pll3_config(void)
{
    HAL_StatusTypeDef status;

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
    RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL3.PLLSource = RCC_PLLSOURCE_HSE;

    /* PLL3 配置: 25MHz / 5 * 250 / 5 = 250MHz */
    RCC_OscInitStruct.PLL3.PLLM = PLL3_M_DIV;
    RCC_OscInitStruct.PLL3.PLLN = PLL3_N_MUL;
    RCC_OscInitStruct.PLL3.PLLP = 1;
    RCC_OscInitStruct.PLL3.PLLQ = 1;
    RCC_OscInitStruct.PLL3.PLLR = PLL3_R_DIV;

    RCC_OscInitStruct.PLL3.PLLRGE = RCC_PLL3VCIRANGE_2;
    RCC_OscInitStruct.PLL3.PLLVCOSEL = RCC_PLL3VCOWIDE;
    RCC_OscInitStruct.PLL3.PLLFRACN = 0;

    status = HAL_RCC_OscConfig(&RCC_OscInitStruct);
    if (status != HAL_OK) {
        return -1;
    }

    return 0;
}

/**
 * @brief  配置 Flash 延迟
 */
static int rcc_flash_latency_config(void)
{
    /* 800MHz 需要 7 个等待状态 (根据 STM32N6 参考手册) */
    /* 同时启用数据缓存和指令缓存 */

    __HAL_FLASH_ART_ENABLE();       /* 启用 ART 加速器 */
    __HAL_FLASH_DATA_CACHE_ENABLE();
    __HAL_FLASH_INST_CACHE_ENABLE();

    /* 设置 Flash 延迟 */
    __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_800MHZ);

    /* 验证延迟设置 */
    if (__HAL_FLASH_GET_LATENCY() != FLASH_LATENCY_800MHZ) {
        return -1;
    }

    return 0;
}
