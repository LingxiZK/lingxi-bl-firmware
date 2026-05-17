/**
  ******************************************************************************
  * @file    app_npu.c
  * @brief   Neural-ART NPU 集成实现 (STM32Cube.AI X-CUBE-AI)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 基于 STM32Cube.AI X-CUBE-AI 运行时
  * - 输入预处理: 640x480 RAW -> 224x224 RGB
  * - 推理引擎: 输入 -> NPU -> 输出 -> 后处理
  * - 量化: INT8 baseline, INT4 experimental
  ******************************************************************************
  */

#include "app_npu.h"

/* =============================================================================
 * X-CUBE-AI 存根模式
 * ==========================================================================*/
/* X-CUBE-AI 运行时 (STM32Cube.AI) 默认不可用, 提供存根实现.
 * 安装 X-CUBE-AI 后, 取消以下 #error 注释以启用 NPU 加速. */
#if !defined(USE_X_CUBE_AI) || !USE_X_CUBE_AI

/* 私有变量 */
static volatile uint8_t s_npu_initialized = 0;

lingxi_err_t app_npu_init(const npu_config_t *config)
{
    (void)config;
    s_npu_initialized = 1;
    LX_DEBUG_PRINT("NPU stub init (X-CUBE-AI not available)");
    return LINGXI_OK;
}

lingxi_err_t app_npu_load_model_from_flash(npu_model_type_t model_type, uint32_t flash_addr)
{
    (void)model_type; (void)flash_addr;
    return LINGXI_OK;
}

lingxi_err_t app_npu_load_model_from_sdram(npu_model_type_t model_type, uint32_t sdram_addr)
{
    (void)model_type; (void)sdram_addr;
    return LINGXI_OK;
}

lingxi_err_t app_npu_preprocess(const uint8_t *input_frame, uint16_t src_width, uint16_t src_height)
{
    (void)input_frame; (void)src_width; (void)src_height;
    return LINGXI_OK;
}

lingxi_err_t app_npu_run(npu_infer_result_t *result)
{
    if (!s_npu_initialized) return LINGXI_ERR_NOT_INIT;
    memset(result, 0, sizeof(npu_infer_result_t));
    result->obstacle_detected = 1;
    result->edge_detected = 0;
    result->track_target_valid = 0;
    result->inference_time_us = 1000;
    result->timestamp_ms = HAL_GetTick();
    return LINGXI_OK;
}

lingxi_err_t app_npu_postprocess(npu_infer_result_t *result)
{
    (void)result;
    return LINGXI_OK;
}

lingxi_err_t app_npu_get_stats(uint32_t *avg_time_us, uint32_t *peak_time_us)
{
    *avg_time_us = 1000;
    *peak_time_us = 1500;
    return LINGXI_OK;
}

lingxi_err_t app_npu_set_quant_mode(npu_quant_mode_t mode)
{
    (void)mode;
    return LINGXI_OK;
}

#else /* USE_X_CUBE_AI */
/* =============================================================================
 * X-CUBE-AI 真实实现
 * ==========================================================================*/

/* 取消注释以下包含以启用 NPU 加速 */
// #include "ai_platform.h"
// #include "ai_datatypes_defines.h"
static npu_quant_mode_t s_quant_mode = NPU_QUANT_INT8;

/* X-CUBE-AI 句柄 */
static ai_handle s_ai_handle = AI_HANDLE_NULL;
static ai_network_report s_ai_report = {0};

/* 缓冲区 */
static AI_ALIGNED(32) static ai_u8 s_activations[NPU_MODEL_MAX_SIZE];
static AI_ALIGNED(32) static ai_u8 s_input_buffer[NPU_INPUT_SIZE];
static AI_ALIGNED(32) static ai_u8 s_output_buffer[NPU_OUTPUT_SIZE];

/* 性能统计 */
static uint32_t s_infer_count = 0;
static uint32_t s_infer_time_total_us = 0;
static uint32_t s_infer_time_peak_us = 0;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static lingxi_err_t npu_hw_init(void);
static lingxi_err_t npu_ai_init(const npu_config_t *config);
static void npu_preprocess_resize(const uint8_t *src, uint16_t src_w, uint16_t src_h,
                                  uint8_t *dst, uint16_t dst_w, uint16_t dst_h);
static void npu_preprocess_normalize(const uint8_t *src, float *dst, uint32_t size);
static void npu_postprocess_detections(const float *output, npu_infer_result_t *result);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 NPU
 */
lingxi_err_t app_npu_init(const npu_config_t *config)
{
    LX_RETURN_IF_NULL(config);

    memcpy(&s_npu_config, config, sizeof(npu_config_t));

    /* 硬件初始化 */
    lingxi_err_t err = npu_hw_init();
    LX_RETURN_IF_ERR(err);

    /* X-CUBE-AI 运行时初始化 */
    err = npu_ai_init(config);
    LX_RETURN_IF_ERR(err);

    s_npu_initialized = 1;

    LX_DEBUG_PRINT("NPU initialized: model=%d, quant=%d, addr=0x%08X",
                   config->model_type, config->quant_mode, config->model_addr);
    return LINGXI_OK;
}

/**
 * @brief  从 Flash 加载模型
 */
lingxi_err_t app_npu_load_model_from_flash(npu_model_type_t model_type, uint32_t flash_addr)
{
    if (!s_npu_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    /* 通过 XIP 直接访问 Flash 中的模型 */
    /* X-CUBE-AI 支持从 Flash 直接加载权重 */
    s_npu_config.model_addr = flash_addr;
    s_npu_config.model_type = model_type;

    /* 重新初始化网络 */
    ai_error err = ai_network_init(s_ai_handle, NULL);
    if (err.type != AI_ERROR_NONE) {
        LX_ERR_PRINT("NPU model init failed: %d", err.type);
        return LINGXI_ERR_MODEL;
    }

    LX_DEBUG_PRINT("NPU model loaded from Flash: 0x%08X", flash_addr);
    return LINGXI_OK;
}

/**
 * @brief  从 SDRAM 加载模型
 */
lingxi_err_t app_npu_load_model_from_sdram(npu_model_type_t model_type, uint32_t sdram_addr)
{
    if (!s_npu_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    s_npu_config.model_addr = sdram_addr;
    s_npu_config.model_type = model_type;

    /* SDRAM 中的模型已可直接访问 */
    ai_error err = ai_network_init(s_ai_handle, NULL);
    if (err.type != AI_ERROR_NONE) {
        return LINGXI_ERR_MODEL;
    }

    LX_DEBUG_PRINT("NPU model loaded from SDRAM: 0x%08X", sdram_addr);
    return LINGXI_OK;
}

/**
 * @brief  输入预处理
 */
lingxi_err_t app_npu_preprocess(const uint8_t *input_frame, uint16_t src_width, uint16_t src_height)
{
    LX_RETURN_IF_NULL(input_frame);

    if (!s_npu_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    /* 1. 缩放: 640x480 -> 224x224 */
    uint8_t resized[NPU_INPUT_SIZE];
    npu_preprocess_resize(input_frame, src_width, src_height,
                          resized, NPU_INPUT_WIDTH, NPU_INPUT_HEIGHT);

    /* 2. 归一化: uint8 -> float, 并转换为模型输入格式 */
    if (s_quant_mode == NPU_QUANT_INT8) {
        /* INT8: 直接复制, 量化在模型中处理 */
        memcpy(s_input_buffer, resized, NPU_INPUT_SIZE);
    } else {
        /* INT4 或其他模式 */
        /* 需要特殊量化处理 */
        memcpy(s_input_buffer, resized, NPU_INPUT_SIZE);
    }

    return LINGXI_OK;
}

/**
 * @brief  执行 NPU 推理
 */
lingxi_err_t app_npu_run(npu_infer_result_t *result)
{
    LX_RETURN_IF_NULL(result);

    if (!s_npu_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    /* 准备输入/输出数组 */
    ai_buffer ai_input[1];
    ai_buffer ai_output[1];

    ai_input[0] = ai_network_inputs_get(s_ai_handle, 0);
    ai_input[0].data = AI_HANDLE_PTR(s_input_buffer);

    ai_output[0] = ai_network_outputs_get(s_ai_handle, 0);
    ai_output[0].data = AI_HANDLE_PTR(s_output_buffer);

    /* 记录推理开始时间 */
    uint32_t start_us = DWT->CYCCNT / (SystemCoreClock / 1000000);

    /* 执行推理 */
    ai_i32 nbatch = ai_network_run(s_ai_handle, &ai_input[0], &ai_output[0]);
    if (nbatch != 1) {
        LX_ERR_PRINT("NPU inference failed: nbatch=%d", nbatch);
        return LINGXI_ERR_NPU;
    }

    /* 记录推理结束时间 */
    uint32_t end_us = DWT->CYCCNT / (SystemCoreClock / 1000000);
    uint32_t infer_time_us = end_us - start_us;

    /* 更新统计 */
    s_infer_count++;
    s_infer_time_total_us += infer_time_us;
    if (infer_time_us > s_infer_time_peak_us) {
        s_infer_time_peak_us = infer_time_us;
    }

    /* 后处理 */
    result->inference_time_us = infer_time_us;
    result->timestamp_ms = xTaskGetTickCount();

    lingxi_err_t err = app_npu_postprocess(result);
    LX_RETURN_IF_ERR(err);

    return LINGXI_OK;
}

/**
 * @brief  后处理
 */
lingxi_err_t app_npu_postprocess(npu_infer_result_t *result)
{
    LX_RETURN_IF_NULL(result);

    /* 解析 NPU 输出 */
    float *output = (float *)s_output_buffer;
    npu_postprocess_detections(output, result);

    /* 设置状态标志 */
    result->obstacle_detected = (result->num_detections > 0);
    result->edge_detected = false;  /* 由专用模型设置 */
    result->track_target_valid = false;  /* 由跟踪模型设置 */

    return LINGXI_OK;
}

/**
 * @brief  获取 NPU 统计
 */
lingxi_err_t app_npu_get_stats(uint32_t *avg_time_us, uint32_t *peak_time_us)
{
    LX_RETURN_IF_NULL(avg_time_us);
    LX_RETURN_IF_NULL(peak_time_us);

    if (s_infer_count > 0) {
        *avg_time_us = s_infer_time_total_us / s_infer_count;
    } else {
        *avg_time_us = 0;
    }
    *peak_time_us = s_infer_time_peak_us;

    return LINGXI_OK;
}

/**
 * @brief  设置量化模式
 */
lingxi_err_t app_npu_set_quant_mode(npu_quant_mode_t mode)
{
    s_quant_mode = mode;
    LX_DEBUG_PRINT("NPU quant mode set to %d", mode);
    return LINGXI_OK;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  NPU 硬件初始化
 */
static lingxi_err_t npu_hw_init(void)
{
    /* 使能 NPU 时钟 */
    /* __HAL_RCC_NPU_CLK_ENABLE(); */

    /* 配置 NPU 电源域 */
    /* 根据 STM32N6 Reference Manual 配置 */

    /* 使能 DWT 周期计数器 (用于性能测量) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    return LINGXI_OK;
}

/**
 * @brief  X-CUBE-AI 运行时初始化
 */
static lingxi_err_t npu_ai_init(const npu_config_t *config)
{
    (void)config;

    /* 获取网络报告 */
    ai_error err = ai_network_get_report(s_ai_handle, &s_ai_report);
    if (err.type != AI_ERROR_NONE) {
        LX_ERR_PRINT("AI network get report failed");
        return LINGXI_ERR_NPU;
    }

    LX_DEBUG_PRINT("AI Network: inputs=%d, outputs=%d, params=%d bytes",
                   s_ai_report.n_inputs, s_ai_report.n_outputs,
                   (int)s_ai_report.params_size);

    return LINGXI_OK;
}

/**
 * @brief  图像缩放 (最近邻插值, 快速)
 */
static void npu_preprocess_resize(const uint8_t *src, uint16_t src_w, uint16_t src_h,
                                  uint8_t *dst, uint16_t dst_w, uint16_t dst_h)
{
    /* RAW10/RAW8 -> RGB 转换 + 缩放 */
    /* 简化实现: 假设输入为灰度或 Bayer, 转换为 RGB */

    uint32_t x_ratio = ((src_w << 16) / dst_w) + 1;
    uint32_t y_ratio = ((src_h << 16) / dst_h) + 1;

    for (uint16_t y = 0; y < dst_h; y++) {
        for (uint16_t x = 0; x < dst_w; x++) {
            uint32_t src_x = (x * x_ratio) >> 16;
            uint32_t src_y = (y * y_ratio) >> 16;

            uint32_t src_idx = src_y * src_w + src_x;
            uint32_t dst_idx = (y * dst_w + x) * 3;

            /* 灰度转 RGB (简化) */
            uint8_t pixel = src[src_idx];
            dst[dst_idx]     = pixel;  /* R */
            dst[dst_idx + 1] = pixel;  /* G */
            dst[dst_idx + 2] = pixel;  /* B */
        }
    }
}

/**
 * @brief  归一化
 */
static void npu_preprocess_normalize(const uint8_t *src, float *dst, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        dst[i] = (float)src[i] / 255.0f;
    }
}

/**
 * @brief  检测后处理 (NMS + 阈值过滤)
 */
static void npu_postprocess_detections(const float *output, npu_infer_result_t *result)
{
    result->num_detections = 0;

    /* 解析模型输出 (假设为 YOLO 格式) */
    /* output 格式: [x, y, w, h, conf, class0, class1, ...] * num_boxes */

    const float conf_threshold = 0.5f;
    const float nms_threshold = 0.45f;

    /* 临时检测数组 */
    npu_detection_t temp_dets[50];
    uint16_t num_temp = 0;

    /* 解析所有候选框 */
    uint16_t num_boxes = 50;  /* 根据模型输出调整 */
    uint16_t num_classes = 3; /* obstacle, edge, track_target */

    for (uint16_t i = 0; i < num_boxes && num_temp < 50; i++) {
        uint16_t base_idx = i * (5 + num_classes);
        float conf = output[base_idx + 4];

        if (conf < conf_threshold) {
            continue;
        }

        /* 找到最高置信度类别 */
        float max_class_conf = 0;
        uint8_t best_class = 0;
        for (uint8_t c = 0; c < num_classes; c++) {
            float class_conf = output[base_idx + 5 + c];
            if (class_conf > max_class_conf) {
                max_class_conf = class_conf;
                best_class = c;
            }
        }

        float final_conf = conf * max_class_conf;
        if (final_conf < conf_threshold) {
            continue;
        }

        /* 添加到临时数组 */
        temp_dets[num_temp].confidence = final_conf;
        temp_dets[num_temp].x = (uint16_t)(output[base_idx] * CAM_WIDTH);
        temp_dets[num_temp].y = (uint16_t)(output[base_idx + 1] * CAM_HEIGHT);
        temp_dets[num_temp].w = (uint16_t)(output[base_idx + 2] * CAM_WIDTH);
        temp_dets[num_temp].h = (uint16_t)(output[base_idx + 3] * CAM_HEIGHT);
        temp_dets[num_temp].class_id = best_class;
        num_temp++;
    }

    /* NMS (简化版) */
    for (uint16_t i = 0; i < num_temp && result->num_detections < 10; i++) {
        uint8_t suppressed = 0;

        for (uint16_t j = 0; j < result->num_detections; j++) {
            /* 计算 IoU */
            int x1 = (int)temp_dets[i].x;
            int y1 = (int)temp_dets[i].y;
            int w1 = (int)temp_dets[i].w;
            int h1 = (int)temp_dets[i].h;
            int x2 = (int)result->detections[j].x;
            int y2 = (int)result->detections[j].y;
            int w2 = (int)result->detections[j].w;
            int h2 = (int)result->detections[j].h;

            int inter_x = (x1 + w1 < x2 + w2) ? (x1 + w1 - x2) : (x2 + w2 - x1);
            int inter_y = (y1 + h1 < y2 + h2) ? (y1 + h1 - y2) : (y2 + h2 - y1);
            int inter_area = (inter_x > 0 && inter_y > 0) ? inter_x * inter_y : 0;
            int union_area = w1 * h1 + w2 * h2 - inter_area;

            float iou = (union_area > 0) ? (float)inter_area / union_area : 0;

            if (iou > nms_threshold) {
                suppressed = 1;
                break;
            }
        }

        if (!suppressed) {
            result->detections[result->num_detections] = temp_dets[i];
            result->num_detections++;
        }
    }
}

#endif /* USE_X_CUBE_AI */
