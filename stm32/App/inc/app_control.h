/**
  ******************************************************************************
  * @file    app_control.h
  * @brief   主控逻辑头文件 (避障/边缘/跟踪)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 避障: 目标检测 + ToF + 决策
  * - 边缘识别: 轻量DL或传统CV
  * - 跟踪: 单目标跟踪 + 卡尔曼滤波
  ******************************************************************************
  */

#ifndef __APP_CONTROL_H
#define __APP_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"
#include "app_npu.h"
#include "app_fusion.h"

/* =============================================================================
 * 控制模式定义
 * ==========================================================================*/
typedef enum {
    CTRL_MODE_IDLE = 0,         /* 空闲 */
    CTRL_MODE_OBSTACLE_AVOID = 1,  /* 避障模式 */
    CTRL_MODE_EDGE_DETECT = 2,  /* 边缘识别模式 */
    CTRL_MODE_TRACK = 3,        /* 跟踪模式 */
    CTRL_MODE_EMERGENCY = 4,    /* 紧急停止 */
} control_mode_t;

/* =============================================================================
 * 控制指令结构
 * ==========================================================================*/
typedef struct {
    control_mode_t cmd_type;    /* 指令类型 */
    int32_t param1;             /* 参数1: 目标距离 (mm) */
    int32_t param2;             /* 参数2: 航向角 (deg * 10) */
    int32_t param3;             /* 参数3: 速度 (mm/s) */
    uint8_t  emergency_stop;    /* 紧急停止标志 */
    uint32_t timestamp_ms;      /* 时间戳 */

    /* 遥测透传 (由 vTaskControl 填充, vTaskComm 直接发送) */
    uint16_t fused_dist_mm;
    float    confidence;
    uint8_t  obstacle_flag;
    uint8_t  edge_flag;
    uint8_t  track_flag;
    uint16_t inference_time_us;
    uint32_t free_heap_bytes;
} control_cmd_t;

/* =============================================================================
 * 控制状态结构
 * ==========================================================================*/
typedef struct {
    control_mode_t current_mode;    /* 当前模式 */
    control_mode_t last_mode;       /* 上一模式 */
    uint16_t target_distance_mm;    /* 目标距离 */
    int16_t target_heading_deg;     /* 目标航向 */
    int16_t target_speed_mmps;      /* 目标速度 */
    uint8_t emergency_stop;         /* 紧急停止标志 */
    uint32_t mode_entry_time_ms;    /* 模式进入时间 */
    uint32_t last_update_ms;        /* 最后更新时间 */
} control_state_t;

/* =============================================================================
 * 避障参数
 * ==========================================================================*/
#define OBSTACLE_SAFE_DISTANCE_MM     2000    /* 安全距离 2m */
#define OBSTACLE_WARNING_DISTANCE_MM  1500    /* 警告距离 1.5m */
#define OBSTACLE_CRITICAL_DISTANCE_MM 800     /* 临界距离 0.8m */
#define OBSTACLE_AVOID_SPEED_MMPS     500     /* 避障速度 0.5m/s */

/* =============================================================================
 * 边缘识别参数
 * ==========================================================================*/
#define EDGE_CONFIDENCE_THRESHOLD     0.7f    /* 边缘置信度阈值 */
#define EDGE_MIN_WIDTH_PX             50      /* 最小边缘宽度 */

/* =============================================================================
 * 跟踪参数
 * ==========================================================================*/
#define TRACK_MAX_LOST_FRAMES         30      /* 最大丢失帧数 */
#define TRACK_CONFIDENCE_THRESHOLD    0.6f    /* 跟踪置信度阈值 */

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化主控逻辑模块
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_control_init(void);

/**
 * @brief  更新控制逻辑
 * @param  state: 控制状态 (输入/输出)
 * @param  infer_result: NPU 推理结果
 * @param  sensor_data: 传感器融合数据
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_control_update(control_state_t *state,
                                const npu_infer_result_t *infer_result,
                                const sensor_fusion_data_t *sensor_data);

/**
 * @brief  避障决策
 * @param  state: 控制状态
 * @param  infer_result: 推理结果
 * @param  sensor_data: 传感器数据
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_control_obstacle_avoid(control_state_t *state,
                                          const npu_infer_result_t *infer_result,
                                          const sensor_fusion_data_t *sensor_data);

/**
 * @brief  边缘识别决策
 * @param  state: 控制状态
 * @param  infer_result: 推理结果
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_control_edge_detect(control_state_t *state,
                                      const npu_infer_result_t *infer_result);

/**
 * @brief  目标跟踪决策
 * @param  state: 控制状态
 * @param  infer_result: 推理结果
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_control_track(control_state_t *state,
                                const npu_infer_result_t *infer_result);

/**
 * @brief  紧急停止
 * @param  state: 控制状态
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_control_emergency_stop(control_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONTROL_H */
