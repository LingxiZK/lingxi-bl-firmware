/*******************************************************************************
 * @file    i2c1_tof.h
 * @brief   I2C1 HAL Driver Header for VL53L1X ToF Sensor
 * @version 1.0.0
 ******************************************************************************/
#ifndef I2C1_TOF_H
#define I2C1_TOF_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C1 Initialization */
void I2C1_ToF_Init(void);

/* VL53L1X Control */
void VL53L1X_PowerOn(void);
void VL53L1X_PowerOff(void);

/* Configuration */
HAL_StatusTypeDef VL53L1X_Configure(void);

/* Measurement */
uint16_t VL53L1X_GetDistance(void);
uint16_t VL53L1X_GetLastDistance(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C1_TOF_H */
