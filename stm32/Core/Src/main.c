/*******************************************************************************
 * @file    main.c
 * @brief   Lingxi N6 AI Deck - STM32N657 Main Entry (Minimal)
 * @version 1.0.0
 ******************************************************************************/

#include "stm32n6xx_hal.h"

/* Private function prototypes */
static void Error_Handler(void);

/**
 * @brief  Main program entry
 */
int main(void)
{
    /* Reset of all peripherals, Initializes the Flash interface and the Systick */
    HAL_Init();

    /* TODO: Configure system clock using CubeMX generated code */
    /* SystemClock_Config(); */

    /* Enable GPIO clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* Configure PA5 as output (LED) */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* TODO: Initialize peripherals */
    /* - FMC SDRAM (IS42SM16320E-75BLI) */
    /* - XSPI1 Flash (W25Q01JVZEIQ) */
    /* - SDMMC1 (ESP32-C6 SDIO) */
    /* - SDMMC2 (microSD) */
    /* - SPI1 (DWM3000 UWB) */
    /* - SPI2 (BMP585 Barometer) */
    /* - I2C1 (VL53L1X ToF) */
    /* - USART1 (Debug) */
    /* - USART2/3 (Flight Controller) */
    /* - CSI (VD55G1 Camera) */

    /* Main loop */
    while (1)
    {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(500);
    }
}

/**
 * @brief  This function is executed in case of error occurrence.
 */
static void __attribute__((unused)) Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

/**
 * @brief  Assert failed callback
 */
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    /* User can add his own implementation */
    while(1) {}
}
#endif

/**
 * @brief  SysTick Handler
 */
void __attribute__((weak)) SysTick_Handler(void)
{
    HAL_IncTick();
}
