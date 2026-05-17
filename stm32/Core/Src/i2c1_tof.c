/*******************************************************************************
 * @file    i2c1_tof.c
 * @brief   I2C1 HAL Driver for VL53L1X ToF Sensor
 * @version 1.0.0
 * @date    2026-05-15
 ******************************************************************************/

#include "stm32n6xx_hal.h"
#include "i2c1_tof.h"
#include "debug_log.h"

/* VL53L1X I2C Address */
#define VL53L1X_ADDR_DEFAULT        0x29
#define VL53L1X_ADDR_SHIFTED        (VL53L1X_ADDR_DEFAULT << 1)

/* VL53L1X Register Addresses */
#define VL53L1X_REG_IDENTIFICATION_MODEL_ID    0x010F
#define VL53L1X_REG_SYSTEM_INTERRUPT_CONFIG    0x0046
#define VL53L1X_REG_SYSTEM_RANGE_CONFIG        0x005E
#define VL53L1X_REG_SYSTEM_INTERRUPT_CLEAR     0x0086
#define VL53L1X_REG_RESULT_RANGE_STATUS        0x0089
#define VL53L1X_REG_RESULT_DISTANCE            0x0096
#define VL53L1X_REG_SOFT_RESET                 0x0000

/* Timing Parameters (400kHz Fast Mode) */
#define I2C1_TIMING_400KHZ          0x00702681UL

static I2C_HandleTypeDef hi2c1;
static uint8_t vl53l1x_addr = VL53L1X_ADDR_SHIFTED;
static uint16_t g_last_distance_mm = 0;

/*============================================================================
 * I2C1 Initialization
 *===========================================================================*/
void I2C1_ToF_Init(void)
{
    LOG_I("I2C1 ToF: Initializing...");

    /* Enable clocks */
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB8=SCL, PB9=SDA */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* XSHUT (PB10) - Output, active high */
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET); /* Shutdown */

    /* GPIO1 (PB11) - Input interrupt */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI11_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI11_IRQn);

    /* I2C1 Configuration: 400kHz Fast Mode */
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = I2C1_TIMING_400KHZ;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        LOG_E("I2C1 ToF: HAL_I2C_Init failed");
        return;
    }

    /* Analog filter enable */
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
    {
        LOG_E("I2C1 ToF: ConfigAnalogFilter failed");
        return;
    }

    /* Digital filter */
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
    {
        LOG_E("I2C1 ToF: ConfigDigitalFilter failed");
        return;
    }

    LOG_I("I2C1 ToF: Initialized at 400kHz");
}

/*============================================================================
 * VL53L1X Sensor Control
 *===========================================================================*/

void VL53L1X_PowerOn(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
    HAL_Delay(2); /* Boot time */
    LOG_I("VL53L1X: Powered on");
}

void VL53L1X_PowerOff(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
    LOG_I("VL53L1X: Powered off");
}

/*============================================================================
 * Register Read/Write (16-bit address)
 *===========================================================================*/
static HAL_StatusTypeDef VL53L1X_ReadReg16(uint16_t reg, uint16_t *value)
{
    uint8_t tx_buf[2] = {(reg >> 8) & 0xFF, reg & 0xFF};
    uint8_t rx_buf[2] = {0};

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(&hi2c1, vl53l1x_addr, tx_buf, 2, 100);
    if (status != HAL_OK) return status;

    status = HAL_I2C_Master_Receive(&hi2c1, vl53l1x_addr, rx_buf, 2, 100);
    if (status == HAL_OK)
    {
        *value = ((uint16_t)rx_buf[0] << 8) | rx_buf[1];
    }
    return status;
}

static HAL_StatusTypeDef VL53L1X_WriteReg16(uint16_t reg, uint16_t value)
{
    uint8_t tx_buf[4] = {(reg >> 8) & 0xFF, reg & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
    return HAL_I2C_Master_Transmit(&hi2c1, vl53l1x_addr, tx_buf, 4, 100);
}

static HAL_StatusTypeDef VL53L1X_ReadReg8(uint16_t reg, uint8_t *value)
{
    uint8_t tx_buf[2] = {(reg >> 8) & 0xFF, reg & 0xFF};
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(&hi2c1, vl53l1x_addr, tx_buf, 2, 100);
    if (status != HAL_OK) return status;
    return HAL_I2C_Master_Receive(&hi2c1, vl53l1x_addr, value, 1, 100);
}

/*============================================================================
 * VL53L1X Configuration
 *===========================================================================*/
HAL_StatusTypeDef VL53L1X_Configure(void)
{
    uint16_t model_id = 0;
    HAL_StatusTypeDef status;

    /* Power on and wait for boot */
    VL53L1X_PowerOn();
    HAL_Delay(10);

    /* Check device ID */
    status = VL53L1X_ReadReg16(VL53L1X_REG_IDENTIFICATION_MODEL_ID, &model_id);
    if (status != HAL_OK)
    {
        LOG_E("VL53L1X: Failed to read model ID (status=%d)", status);
        return HAL_ERROR;
    }

    LOG_I("VL53L1X: Model ID = 0x%04X", model_id);

    if (model_id != 0xEACC) /* VL53L1X expected ID */
    {
        LOG_W("VL53L1X: Unexpected model ID (expected 0xEACC, got 0x%04X)", model_id);
    }

    /* TODO: Load VL53L1X default configuration from API */
    /* Full configuration requires ST VL53L1X API (vl53l1x_api.lib) */

    LOG_I("VL53L1X: Configured");
    return HAL_OK;
}

/*============================================================================
 * Distance Measurement
 *===========================================================================*/
uint16_t VL53L1X_GetDistance(void)
{
    uint16_t distance = 0xFFFF;
    HAL_StatusTypeDef status;

    /* Trigger single measurement */
    /* Simplified: read result register directly */
    status = VL53L1X_ReadReg16(VL53L1X_REG_RESULT_DISTANCE, &distance);
    if (status == HAL_OK)
    {
        g_last_distance_mm = distance;
    }
    else
    {
        LOG_W("VL53L1X: Read distance failed (status=%d)", status);
    }

    return g_last_distance_mm;
}

uint16_t VL53L1X_GetLastDistance(void)
{
    return g_last_distance_mm;
}

/*============================================================================
 * Interrupt Handler
 *===========================================================================*/
void EXTI11_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_11) != RESET)
    {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_11);
        /* ToF measurement ready callback */
        /* TODO: Signal RTOS task */
    }
}
