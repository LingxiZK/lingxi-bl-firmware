/*
  * @file    stm32n6xx_hal_conf.h
  * @brief   HAL configuration file for Lingxi N6 AI Deck
  */

#ifndef STM32N6XX_HAL_CONF_H
#define STM32N6XX_HAL_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_SDRAM_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_MCE_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_RIF_MODULE_ENABLED
#define HAL_SDMMC_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_USART_MODULE_ENABLED
#define HAL_XSPI_MODULE_ENABLED

/* ########################## Oscillator Values ############################# */
#if !defined  (HSE_VALUE)
  #define HSE_VALUE    48000000U
#endif

#if !defined  (HSI_VALUE)
  #define HSI_VALUE    64000000U
#endif

#if !defined  (LSI_VALUE)
  #define LSI_VALUE    32000U
#endif

#if !defined  (LSE_VALUE)
  #define LSE_VALUE    32768U
#endif

/* ########################## Timeout Values ############################# */
#if !defined  (HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    100U
#endif

#if !defined  (LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT    5000U
#endif

/* ########################### System Configuration ######################### */
#define  VDD_VALUE                    3300U
#define  TICK_INT_PRIORITY            ((uint32_t)(1U<<4U))
#define  USE_RTOS                     0U
#define  PREFETCH_ENABLE              1U
#define  ART_ACCELERATOR_ENABLE       1U

/* ########################## Assert Selection ############################## */
#ifdef USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t* file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif

/* Includes ------------------------------------------------------------------*/
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32n6xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32n6xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32n6xx_hal_dma.h"
#endif

#ifdef HAL_EXTI_MODULE_ENABLED
  #include "stm32n6xx_hal_exti.h"
#endif

#ifdef HAL_SDRAM_MODULE_ENABLED
  #include "stm32n6xx_hal_sdram.h"
#endif

#ifdef HAL_I2C_MODULE_ENABLED
  #include "stm32n6xx_hal_i2c.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32n6xx_hal_pwr.h"
#endif

#ifdef HAL_SDMMC_MODULE_ENABLED
  #include "stm32n6xx_hal_sd.h"
#endif

#ifdef HAL_SPI_MODULE_ENABLED
  #include "stm32n6xx_hal_spi.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32n6xx_hal_uart.h"
#endif

#ifdef HAL_USART_MODULE_ENABLED
  #include "stm32n6xx_hal_usart.h"
#endif

#ifdef HAL_XSPI_MODULE_ENABLED
  #include "stm32n6xx_hal_xspi.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32n6xx_hal_cortex.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32N6XX_HAL_CONF_H */
