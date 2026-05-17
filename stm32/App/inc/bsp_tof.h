/**
  ******************************************************************************
  * @file    bsp_tof.h
  * @brief   VL53L1X ToF 驱动头文件 (I2C1, PB8-PB11, 400kHz)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - VL53L1X: ST FlightSense 长距离 ToF 传感器
  * - 最大 4m 测距, 30Hz 更新率
  * - I2C1 @ 400kHz
  ******************************************************************************
  */

#ifndef __BSP_TOF_H
#define __BSP_TOF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * ToF 配置参数
 * ==========================================================================*/
#define TOF_I2C_ADDR                0x29    /* VL53L1X 默认 I2C 地址 */
#define TOF_I2C_FREQ                400000  /* 400 kHz */
#define TOF_MEASUREMENT_PERIOD_MS   33      /* 30Hz = 33ms */
#define TOF_TIMING_BUDGET_US        20000   /* 20ms timing budget */
#define TOF_DISTANCE_MAX_MM         4000    /* 最大测距 4m */

/* VL53L1X 寄存器 (简化版, 实际使用 ST API) */
#define VL53L1X_REG_IDENTIFICATION_MODEL_ID     0x010F
#define VL53L1X_REG_SYSTEM_INTERRUPT_GPIO       0x0B0B
#define VL53L1X_REG_RESULT_RANGE_STATUS        0x0089

/* =============================================================================
 * 数据结构
 * ==========================================================================*/

typedef struct {
    uint16_t distance_mm;       /* 距离 (毫米) */
    uint16_t signal_rate;       /* 信号率 */
    uint16_t ambient_rate;      /* 环境光率 */
    uint8_t  range_status;      /* 测距状态 */
    uint32_t timestamp_ms;      /* 时间戳 */
    uint8_t  valid;             /* 数据有效 */
} tof_measurement_t;

/* 回调函数类型 */
typedef void (*tof_callback_t)(const tof_measurement_t *meas);

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 VL53L1X ToF 传感器
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_tof_init(void);

/**
 * @brief  启动连续测距
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_tof_start(void);

/**
 * @brief  停止连续测距
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_tof_stop(void);

/**
 * @brief  单次测距 (阻塞式)
 * @param  meas: 输出测量结果
 * @param  timeout_ms: 超时时间
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_tof_single_shot(tof_measurement_t *meas, uint32_t timeout_ms);

/**
 * @brief  注册测量回调
 * @param  callback: 回调函数
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_tof_register_callback(tof_callback_t callback);

/**
 * @brief  获取最新测量结果
 * @param  meas: 输出测量结果
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_tof_get_measurement(tof_measurement_t *meas);

/**
 * @brief  ToF 中断服务程序
 */
void bsp_tof_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TOF_H */
