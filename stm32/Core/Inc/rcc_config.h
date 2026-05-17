/**
  ******************************************************************************
  * @file    rcc_config.h
  * @brief   RCC 时钟配置头文件 (STM32N657L0H3Q) — P0批次
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 系统时钟: 800MHz (HCLK)
  * - FMC 时钟: 166MHz (SDRAM)
  * - MIPI 时钟: 250MHz
  * - APB1/APB2/APB3: 200MHz
  * - 使用 HSE 25MHz 外部晶振
  ******************************************************************************
  */

#ifndef __RCC_CONFIG_H
#define __RCC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * 时钟频率定义
 * ==========================================================================*/
#define HSE_VALUE                   25000000U       /* HSE: 25 MHz */
#define HSI_VALUE                   64000000U       /* HSI: 64 MHz */

#define SYSCLK_FREQ                 800000000U      /* SYSCLK: 800 MHz */
#define HCLK_FREQ                   SYSCLK_FREQ     /* HCLK = SYSCLK */
#define PCLK1_FREQ                  200000000U      /* APB1: 200 MHz */
#define PCLK2_FREQ                  200000000U      /* APB2: 200 MHz */
#define PCLK3_FREQ                  200000000U      /* APB3: 200 MHz */
#define PCLK4_FREQ                  200000000U      /* APB4: 200 MHz */

#define FMC_CLK_FREQ                166000000U      /* FMC: 166 MHz */
#define MIPI_CLK_FREQ               250000000U      /* MIPI CSI-2: 250 MHz */
#define SDIO_CLK_FREQ               50000000U       /* SDIO: 50 MHz max */
#define UART1_CLK_FREQ              200000000U      /* UART1: 200 MHz */

/* =============================================================================
 * PLL 配置参数
 * ==========================================================================*/
/* PLL1: 800MHz 系统时钟 */
#define PLL1_SOURCE                 RCC_PLLSOURCE_HSE
#define PLL1_M_DIV                  5               /* 25MHz / 5 = 5MHz */
#define PLL1_N_MUL                  160             /* 5MHz * 160 = 800MHz */
#define PLL1_P_DIV                  1               /* PLL1_P = 800MHz (SYSCLK) */
#define PLL1_Q_DIV                  4               /* PLL1_Q = 200MHz (APB) */
#define PLL1_R_DIV                  5               /* PLL1_R = 160MHz (FMC base) */

/* PLL2: 166MHz FMC 时钟 */
#define PLL2_M_DIV                  5               /* 25MHz / 5 = 5MHz */
#define PLL2_N_MUL                  332             /* 5MHz * 332 = 1660MHz -> /10 = 166MHz */
#define PLL2_R_DIV                  10              /* PLL2_R = 166MHz (FMC) */

/* PLL3: 250MHz MIPI 时钟 */
#define PLL3_M_DIV                  5               /* 25MHz / 5 = 5MHz */
#define PLL3_N_MUL                  250             /* 5MHz * 250 = 1250MHz -> /5 = 250MHz */
#define PLL3_R_DIV                  5               /* PLL3_R = 250MHz (MIPI) */

/* =============================================================================
 * 外设时钟使能位定义 (简化版, 兼容 HAL)
 * ==========================================================================*/
/* AHB1 外设 */
#define RCC_AHB1ENR_GPIOA_EN        (1U << 0)
#define RCC_AHB1ENR_GPIOB_EN        (1U << 1)
#define RCC_AHB1ENR_GPIOC_EN        (1U << 2)
#define RCC_AHB1ENR_GPIOD_EN        (1U << 3)
#define RCC_AHB1ENR_GPIOE_EN        (1U << 4)
#define RCC_AHB1ENR_GPIOF_EN        (1U << 5)
#define RCC_AHB1ENR_GPIOG_EN        (1U << 6)
#define RCC_AHB1ENR_GPIOH_EN        (1U << 7)
#define RCC_AHB1ENR_GPIOI_EN        (1U << 8)
#define RCC_AHB1ENR_DMA1_EN         (1U << 16)
#define RCC_AHB1ENR_DMA2_EN         (1U << 17)
#define RCC_AHB1ENR_MDMA_EN         (1U << 18)

/* AHB2 外设 */
#define RCC_AHB2ENR_SDMMC1_EN       (1U << 0)
#define RCC_AHB2ENR_SDMMC2_EN       (1U << 1)
#define RCC_AHB2ENR_OCTOSPI1_EN     (1U << 4)
#define RCC_AHB2ENR_OCTOSPI2_EN     (1U << 5)

/* AHB3 外设 */
#define RCC_AHB3ENR_FMC_EN          (1U << 0)
#define RCC_AHB3ENR_MIPI_EN         (1U << 4)

/* APB1 外设 */
#define RCC_APB1ENR_USART1_EN       (1U << 4)
#define RCC_APB1ENR_USART2_EN       (1U << 5)
#define RCC_APB1ENR_SPI2_EN         (1U << 14)
#define RCC_APB1ENR_I2C1_EN         (1U << 21)
#define RCC_APB1ENR_I2C2_EN         (1U << 22)

/* APB2 外设 */
#define RCC_APB2ENR_SPI1_EN         (1U << 12)
#define RCC_APB2ENR_SPI3_EN         (1U << 15)
#define RCC_APB2ENR_USART6_EN       (1U << 5)

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  配置系统时钟 (800MHz)
 * @retval 0 成功, 非0 失败
 */
int rcc_system_clock_config(void);

/**
 * @brief  使能外设时钟
 * @param  ahb1_mask: AHB1 外设时钟掩码
 * @param  ahb2_mask: AHB2 外设时钟掩码
 * @param  ahb3_mask: AHB3 外设时钟掩码
 * @param  apb1_mask: APB1 外设时钟掩码
 * @param  apb2_mask: APB2 外设时钟掩码
 */
void rcc_periph_clock_enable(uint32_t ahb1_mask, uint32_t ahb2_mask,
                              uint32_t ahb3_mask, uint32_t apb1_mask,
                              uint32_t apb2_mask);

/**
 * @brief  获取当前系统时钟频率 (Hz)
 * @retval HCLK 频率
 */
uint32_t rcc_get_sysclk_freq(void);

/**
 * @brief  获取当前 HCLK 频率 (Hz)
 * @retval HCLK 频率
 */
uint32_t rcc_get_hclk_freq(void);

/**
 * @brief  获取当前 PCLK1 频率 (Hz)
 * @retval PCLK1 频率
 */
uint32_t rcc_get_pclk1_freq(void);

/**
 * @brief  获取当前 PCLK2 频率 (Hz)
 * @retval PCLK2 频率
 */
uint32_t rcc_get_pclk2_freq(void);

/**
 * @brief  配置 FMC 时钟 (166MHz)
 * @retval 0 成功
 */
int rcc_fmc_clock_config(void);

/**
 * @brief  配置 MIPI 时钟 (250MHz)
 * @retval 0 成功
 */
int rcc_mipi_clock_config(void);

/**
 * @brief  配置 SDIO 时钟 (25-50MHz)
 * @param  freq_hz: 目标频率 (Hz)
 * @retval 0 成功
 */
int rcc_sdio_clock_config(uint32_t freq_hz);

#ifdef __cplusplus
}
#endif

#endif /* __RCC_CONFIG_H */
