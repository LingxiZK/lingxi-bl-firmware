/**
  ******************************************************************************
  * @file    app_npu.h
  * @brief   Neural-ART NPU 集成头文件 (STM32Cube.AI X-CUBE-AI)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - STM32N657 Neural-ART NPU @ 1GHz
  * - 支持 INT8 baseline + INT4 实验
  * - 模型从 Flash/SDRAM 加载
  ******************************************************************************
  */

#ifndef __APP_NPU_H
#define __APP_NPU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * NPU 配置参数
 * ==========================================================================*/
#define NPU_MODEL_ADDR_FLASH        0x70000000U     /* Flash XIP 地址 */
#define NPU_MODEL_ADDR_SDRAM        0xC0000000U     /* SDRAM 地址 */
#define NPU_MODEL_MAX_SIZE          (8 * 1024 * 1024)  /* 最大 8MB 模型 */
#define NPU_INPUT_WIDTH             224             /* 模型输入宽度 */
#define NPU_INPUT_HEIGHT            224             /* 模型输入高度 */
#define NPU_INPUT_CHANNELS          3               /* RGB */
#define NPU_INPUT_SIZE              (NPU_INPUT_WIDTH * NPU_INPUT_HEIGHT * NPU_INPUT_CHANNELS)
#define NPU_OUTPUT_SIZE             256             /* 输出缓冲区大小 */

/* 量化模式 */
typedef enum {
    NPU_QUANT_INT8 = 0,         /* INT8 baseline */
    NPU_QUANT_INT4 = 1,         /* INT4 实验模式 */
} npu_quant_mode_t;

/* 推理模型类型 */
typedef enum {
    NPU_MODEL_OBSTACLE = 0,     /* 避障检测模型 */
    NPU_MODEL_EDGE = 1,         /* 边缘识别模型 */
    NPU_MODEL_TRACK = 2,        /* 目标跟踪模型 */
} npu_model_type_t;

/* =============================================================================
 * 数据结构
 * ==========================================================================*/

typedef struct {
    float confidence;           /* 置信度 0.0-1.0 */
    uint16_t x;                 /* 边界框 X */
    uint16_t y;                 /* 边界框 Y */
    uint16_t w;                 /* 边界框宽度 */
    uint16_t h;                 /* 边界框高度 */
    uint8_t class_id;           /* 类别 ID */
} npu_detection_t;

typedef struct {
    uint8_t obstacle_detected;    /* 检测到障碍物 */
    uint8_t edge_detected;      /* 检测到边缘 */
    uint8_t track_target_valid; /* 跟踪目标有效 */
    uint16_t num_detections;    /* 检测目标数量 */
    npu_detection_t detections[10];  /* 最多 10 个目标 */
    float track_x;              /* 跟踪目标 X */
    float track_y;              /* 跟踪目标 Y */
    uint32_t inference_time_us; /* 推理耗时 (us) */
    uint32_t timestamp_ms;      /* 时间戳 */
} npu_infer_result_t;

typedef struct {
    npu_model_type_t model_type;
    npu_quant_mode_t quant_mode;
    uint32_t model_addr;        /* 模型加载地址 */
    uint32_t model_size;        /* 模型大小 */
    uint32_t activations_addr;  /* 激活值缓冲区地址 */
    uint32_t activations_size;  /* 激活值缓冲区大小 */
} npu_config_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 NPU 和 X-CUBE-AI 运行时
 * @param  config: NPU 配置
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_npu_init(const npu_config_t *config);

/**
 * @brief  从 Flash 加载模型
 * @param  model_type: 模型类型
 * @param  flash_addr: Flash 地址
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_npu_load_model_from_flash(npu_model_type_t model_type, uint32_t flash_addr);

/**
 * @brief  从 SDRAM 加载模型
 * @param  model_type: 模型类型
 * @param  sdram_addr: SDRAM 地址
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_npu_load_model_from_sdram(npu_model_type_t model_type, uint32_t sdram_addr);

/**
 * @brief  输入预处理 (缩放/归一化/量化)
 * @param  input_frame: 输入帧 (CAM_WIDTH x CAM_HEIGHT)
 * @param  src_width: 源图像宽度
 * @param  src_height: 源图像高度
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_npu_preprocess(const uint8_t *input_frame, uint16_t src_width, uint16_t src_height);

/**
 * @brief  执行 NPU 推理
 * @param  result: 输出推理结果
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_npu_run(npu_infer_result_t *result);

/**
 * @brief  后处理 (NMS / 阈值过滤)
 * @param  result: 推理结果
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_npu_postprocess(npu_infer_result_t *result);

/**
 * @brief  获取 NPU 性能统计
 * @param  avg_time_us: 平均推理时间 (us)
 * @param  peak_time_us: 峰值推理时间 (us)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_npu_get_stats(uint32_t *avg_time_us, uint32_t *peak_time_us);

/**
 * @brief  设置量化模式
 * @param  mode: 量化模式
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_npu_set_quant_mode(npu_quant_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* __APP_NPU_H */
