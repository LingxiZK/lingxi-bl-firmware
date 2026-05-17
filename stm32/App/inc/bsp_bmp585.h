/**
  ******************************************************************************
  * @file    bsp_bmp585.h
  * @brief   BMP585 气压传感器 SPI2 驱动头文件 (PB12-PB15 + PD3 INT)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - SPI2 @ 10MHz max
  * - Bosch BMP585 气压传感器 (300-1250 hPa, 精度 ±0.006 hPa)
  * - INT 连接 PD3 (GPIO EXTI)
  ******************************************************************************
  */

#ifndef __BSP_BMP585_H
#define __BSP_BMP585_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * BMP585 配置参数
 * ==========================================================================*/
#define BMP585_SPI_BAUDRATE         10000000        /* 10 MHz max */
#define BMP585_SPI_TIMEOUT_MS       100
#define BMP585_INIT_RETRIES         3

/* BMP585 寄存器地址 */
#define BMP585_REG_CHIP_ID          0x00
#define BMP585_REG_ERR_REG          0x02
#define BMP585_REG_STATUS           0x03
#define BMP585_REG_PRESS_DATA_XLSB  0x04
#define BMP585_REG_PRESS_DATA_LSB   0x05
#define BMP585_REG_PRESS_DATA_MSB   0x06
#define BMP585_REG_TEMP_DATA_XLSB   0x07
#define BMP585_REG_TEMP_DATA_LSB    0x08
#define BMP585_REG_TEMP_DATA_MSB    0x09
#define BMP585_REG_INT_STATUS       0x0A
#define BMP585_REG_FIFO_DATA        0x0B
#define BMP585_REG_FIFO_LENGTH_0    0x0C
#define BMP585_REG_FIFO_LENGTH_1    0x0D
#define BMP585_REG_FIFO_WTM_0       0x0E
#define BMP585_REG_FIFO_WTM_1       0x0F
#define BMP585_REG_FIFO_CONFIG      0x10
#define BMP585_REG_INT_CONFIG       0x11
#define BMP585_REG_PWR_CTRL         0x12
#define BMP585_REG_OSR_CONFIG       0x13
#define BMP585_REG_ODR_CONFIG       0x14
#define BMP585_REG_DSP_CONFIG       0x15
#define BMP585_REG_DSP_IIR          0x16
#define BMP585_REG_CMD              0x7E

/* 期望 Chip ID */
#define BMP585_CHIP_ID              0x50

/* 测量模式 */
#define BMP585_MODE_SLEEP           0x00
#define BMP585_MODE_FORCED          0x01
#define BMP585_MODE_NORMAL          0x03

/* =============================================================================
 * 数据结构
 * ==========================================================================*/
typedef struct {
    float pressure_hpa;     /* 气压 (hPa) */
    float temperature_c;    /* 温度 (°C) */
    uint32_t timestamp_ms;  /* 时间戳 */
} bmp585_data_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 BMP585 SPI2 接口
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_bmp585_init(void);

/**
 * @brief  BMP585 软复位
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_bmp585_soft_reset(void);

/**
 * @brief  读取 BMP585 Chip ID
 * @param  chip_id: 输出缓冲区
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_bmp585_read_chip_id(uint8_t *chip_id);

/**
 * @brief  配置 BMP585 测量模式
 * @param  mode: BMP585_MODE_SLEEP / BMP585_MODE_FORCED / BMP585_MODE_NORMAL
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_bmp585_set_mode(uint8_t mode);

/**
 * @brief  读取压力和温度
 * @param  data: 输出数据结构
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_bmp585_read_data(bmp585_data_t *data);

/**
 * @brief  BMP585 INT 中断处理函数 (在 EXTI 中断服务程序中调用)
 */
void bsp_bmp585_irq_handler(void);

/**
 * @brief  检查 BMP585 INT 状态
 * @retval true INT 低电平 (数据就绪)
 */
bool bsp_bmp585_is_data_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BMP585_H */
