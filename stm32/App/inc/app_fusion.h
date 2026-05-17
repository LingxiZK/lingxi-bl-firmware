/**
  ******************************************************************************
  * @file    app_fusion.h
  * @brief   传感器融合头文件 (ToF + UWB + 视觉)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - ToF: 30Hz, UWB: 10Hz, 视觉: 30-50fps
  * - 卡尔曼滤波融合
  * - 输出统一的空间感知数据
  ******************************************************************************
  */

#ifndef __APP_FUSION_H
#define __APP_FUSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * 传感器融合配置
 * ==========================================================================*/
#define FUSION_TOF_WEIGHT           0.4f    /* ToF 权重 */
#define FUSION_UWB_WEIGHT           0.3f    /* UWB 权重 */
#define FUSION_VISION_WEIGHT        0.3f    /* 视觉权重 */
#define FUSION_MIN_VALID_DISTANCE   50      /* 最小有效距离 50mm */
#define FUSION_MAX_VALID_DISTANCE   10000   /* 最大有效距离 10m */

/* =============================================================================
 * 数据结构
 * ==========================================================================*/

typedef struct {
    uint16_t tof_distance_mm;   /* ToF 距离 */
    uint8_t  tof_valid;         /* ToF 有效 */
    float    uwb_distance_m;    /* UWB 距离 */
    uint8_t  uwb_valid;         /* UWB 有效 */
    uint16_t vision_distance_mm; /* 视觉估算距离 */
    uint8_t  vision_valid;      /* 视觉有效 */
    float    fused_distance_mm; /* 融合后距离 */
    float    confidence;        /* 融合置信度 0.0-1.0 */
    uint32_t timestamp_ms;      /* 时间戳 */
} sensor_fusion_data_t;

typedef struct {
    float x;                    /* 位置 X (mm) */
    float y;                    /* 位置 Y (mm) */
    float z;                    /* 位置 Z (mm) */
    float vx;                   /* 速度 X (mm/s) */
    float vy;                   /* 速度 Y (mm/s) */
    float vz;                   /* 速度 Z (mm/s) */
    float P[6][6];              /* 协方差矩阵 */
} kalman_state_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化传感器融合模块
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_fusion_init(void);

/**
 * @brief  更新传感器数据并执行融合
 * @param  data: 传感器输入数据
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_fusion_update(sensor_fusion_data_t *data);

/**
 * @brief  获取融合结果
 * @param  data: 输出融合数据
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_fusion_get_result(sensor_fusion_data_t *data);

/**
 * @brief  卡尔曼滤波预测步骤
 * @param  dt_ms: 时间间隔 (ms)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_fusion_kalman_predict(uint32_t dt_ms);

/**
 * @brief  卡尔曼滤波更新步骤
 * @param  measurement: 测量值 (mm)
 * @param  sensor_type: 传感器类型 (0=ToF, 1=UWB, 2=Vision)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_fusion_kalman_update(float measurement, uint8_t sensor_type);

#ifdef __cplusplus
}
#endif

#endif /* __APP_FUSION_H */
