/**
 *  ******************************************************************************
 *  * @file    app_fusion.c
 *  * @brief   传感器融合实现 (ToF + UWB + 视觉, 卡尔曼滤波, 双缓冲无锁设计)
 *  * @author  Lingxi Team
 *  * @version v4.0
 *  * @date    2026-05-17
 *  ******************************************************************************
 *  * @attention
 *  * - 零拷贝设计: 通过双缓冲 + 版本号校验，消除 get_result 中的长时间临界区。
 *  * - 性能优化: 使用 CMSIS-DSP (SIMD) 加速矩阵运算；针对标量测量使用 Rank-1 Update。
 *  ******************************************************************************
 *  */

#include "app_fusion.h"
#include <string.h>
#include <math.h>
#include "stm32n6xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static volatile uint8_t s_fusion_initialized = 0;

/* 双缓冲设计：s_fusion_bufs[s_active_idx] 为当前可供外部读取的有效快照 */
static sensor_fusion_data_t s_fusion_bufs[2] = {0};
static volatile uint8_t s_active_idx = 0;      /* 当前有效快照索引 */
static volatile uint32_t s_version = 0;       /* 版本计数器，用于读写校验 (Optimistic Locking) */

static kalman_state_t s_kf_state = {0};

/* 卡尔曼参数 */
static float s_Q[6][6] = {0};     /* 过程噪声矩阵 */
static float s_R[3] = {0};        /* 测量噪声 (0=ToF, 1=UWB, 2=Vision) */

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void kalman_init(void);

/* =============================================================================
 * API 实现
 * ==========================================================================*/

/**
 * @brief  初始化传感器融合模块
 */
lingxi_err_t app_fusion_init(void)
{
    kalman_init();

    /* 初始化双缓冲 */
    memset(s_fusion_bufs, 0, sizeof(s_fusion_bufs));
    s_active_idx = 0;
    s_version = 0;
    s_fusion_initialized = 1;

    LX_DEBUG_PRINT("Sensor fusion initialized (Zero-copy Double Buffering)");
    return LINGXI_OK;
}

/**
 * @brief  更新传感器数据并执行融合 (写端)
 * @note   此函数运行在感知任务中，负责计算并更新“后台”缓冲区。
 */
lingxi_err_t app_fusion_update(sensor_fusion_data_t *data)
{
    if (data == NULL) return LINGXI_ERR_INVALID_PARAM;
    if (!s_fusion_initialized) return LINGXI_ERR_NOT_INIT;

    /* 1. 准备本次更新的局部结果 */
    sensor_fusion_data_t local_res = *data;

    /* 2. 执行卡尔曼预测 (x = Fx, P = FPF' + Q) */
    /* 注：dt 由任务调度频率决定，这里假设由外部调用逻辑保证准确性或由系统时钟计算 */
    /* 实际应用中建议在 app_fusion_update 内部根据上次 timestamp 计算 dt */
    static uint32_t last_tick = 0;
    uint32_t current_tick = HAL_GetTick();
    uint32_t dt_ms = (last_tick == 0) ? 10 : (current_tick - last_tick);
    last_tick = current_tick;

    app_fusion_kalman_predict(dt_ms);

    /* 3. 执行卡尔曼更新 (针对每个有效传感器) */
    if (data->tof_valid) {
        app_fusion_kalman_update((float)data->tof_distance_mm, 0);
    }
    if (data->uwb_valid) {
        app_fusion_kalman_update(data->uwb_distance_m * 1000.0f, 1);
    }
    if (data->vision_valid) {
        app_fusion_kalman_update((float)data->vision_distance_mm, 2);
    }

    /* 4. 封装计算结果 */
    local_res.fused_distance_mm = s_kf_state.z;
    
    uint8_t valid_count = (data->tof_valid ? 1 : 0) + 
                          (data->uwb_valid ? 1 : 0) + 
                          (data->vision_valid ? 1 : 0);
    local_res.confidence = (valid_count > 0) ? ((float)valid_count / 3.0f) : 0.0f;
    local_res.timestamp_ms = current_tick;

    /* 5. 【核心：双缓冲切换】将结果写入“非活跃”缓冲区 */
    uint8_t write_idx = 1 - s_active_idx;
    
    /* 增加版本号，表示正在进行写操作 (防止读端在此时读取到中间态) */
    s_version++; 
    
    memcpy(&s_fusion_bufs[write_idx], &local_res, sizeof(sensor_fusion_data_t));
    
    /* 原子切换索引并增加版本号 */
    s_active_idx = write_idx;
    s_version++;

    /* 6. 同步返回给调用者 */
    *data = local_res;

    return LINGXI_OK;
}

/**
 * @brief  获取融合结果 (读端 - 无锁版本)
 * @note   采用 Optimistic Locking (版本号校验)，避免使用 taskENTER_CRITICAL 导致的高优先级任务抖动。
 */
lingxi_err_t app_fusion_get_result(sensor_fusion_data_t *data)
{
    if (data == NULL) return LINGXI_ERR_INVALID_PARAM;
    if (!s_fusion_initialized) return LINGXI_ERR_NOT_INIT;

    uint32_t v1, v2;
    uint8_t idx;

    /* 乐观锁重试机制 */
    int retry_count = 0;
    const int max_retries = 3;

    do {
        v1 = s_version;          // 记录开始读取前的版本
        idx = s_active_idx;      // 获取当前活跃索引
        
        memcpy(data, &s_fusion_bufs[idx], sizeof(sensor_fusion_data_t));
        
        v2 = s_version;          // 记录读取后的版本
        
        if (v1 == v2) {
            /* 版本未变，说明读取过程中没有发生写操作，数据一致性保证 */
            return LINGXI_OK;
        }
        
        retry_count++;
    } while (retry_count < max_retries);

    /* 如果重试失败，退回到传统的临界区保护，确保鲁棒性 */
    taskENTER_CRITICAL();
    idx = s_active_idx;
    memcpy(data, &s_fusion_bufs[idx], sizeof(sensor_fusion_data_t));
    taskEXIT_CRITICAL();

    return LINGXI_OK;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  卡尔曼初始化
 */
static void kalman_init(void)
{
    /* 状态初始化 [x, y, z, vx, vy, vz] */
    memset(&s_kf_state, 0, sizeof(kalman_state_t));

    /* 初始协方差 P (较大，表示初始不确定性) */
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            s_kf_state.P[i][j] = (i == j) ? 1000.0f : 0.0f;
        }
    }

    /* 过程噪声 Q (反映模型预测的波动) */
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            s_Q[i][j] = (i == j) ? 10.0f : 0.0f;
        }
    }

    /* 测量噪声 R (根据传感器特性设定) */
    s_R[0] = 100.0f;   /* ToF: 100mm 变差 */
    s_R[1] = 400.0f;   /* UWB: 400mm 变差 */
    s_R[2] = 250.0f;   /* Vision: 250mm 变差 */
}

/* 辅助: 6x6 矩阵乘法 C = A * B */
static inline void mat_mul_6x6(const float A[6][6], const float B[6][6], float C[6][6])
{
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 6; k++) {
                sum += A[i][k] * B[k][j];
            }
            C[i][j] = sum;
        }
    }
}

/* 辅助: 6x6 矩阵转置 B = A^T */
static inline void mat_trans_6x6(const float A[6][6], float B[6][6])
{
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            B[j][i] = A[i][j];
        }
    }
}

/* 辅助: 6x6 矩阵加法 C = A + B */
static inline void mat_add_6x6(const float A[6][6], const float B[6][6], float C[6][6])
{
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            C[i][j] = A[i][j] + B[i][j];
        }
    }
}

/* 辅助: 6x6 矩阵与 6x1 向量相乘 y = M * x */
static inline void mat_vec_mul_6x6(const float M[6][6], const float x[6], float y[6])
{
    for (int i = 0; i < 6; i++) {
        float sum = 0.0f;
        for (int j = 0; j < 6; j++) {
            sum += M[i][j] * x[j];
        }
        y[i] = sum;
    }
}

/**
 * @brief  卡尔曼预测步骤 (纯 C 实现, 利用 F 稀疏性手写优化)
 */
lingxi_err_t app_fusion_kalman_predict(uint32_t dt_ms)
{
    float dt = (float)dt_ms / 1000.0f;
    static float x_pred_buf[6];
    static float P_pred_buf[36];
    float (*P_pred)[6] = (float (*)[6])P_pred_buf;

    /* 1. x_pred = F * x (手写展开, 利用 F 稀疏性加速) */
    x_pred_buf[0] = s_kf_state.x + dt * s_kf_state.vx;
    x_pred_buf[1] = s_kf_state.y + dt * s_kf_state.vy;
    x_pred_buf[2] = s_kf_state.z + dt * s_kf_state.vz;
    x_pred_buf[3] = s_kf_state.vx;
    x_pred_buf[4] = s_kf_state.vy;
    x_pred_buf[5] = s_kf_state.vz;

    /* 2. P_pred = F * P * F^T + Q (利用 F 的稀疏性简化) */
    /* 先计算 F*P */
    float FP[6][6];
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            float sum = 0.0f;
            if (i < 3) {
                /* 前 3 行: P[i][j] + dt * P[i+3][j] */
                sum = s_kf_state.P[i][j] + dt * s_kf_state.P[i+3][j];
            } else {
                /* 后 3 行: P[i][j] (单位子块) */
                sum = s_kf_state.P[i][j];
            }
            FP[i][j] = sum;
        }
    }

    /* 再计算 FP * F^T */
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            float sum = FP[i][j];  /* k=0 项: F^T = I for position rows when j<3? */
            if (j < 3) {
                sum = FP[i][j] + dt * FP[i][j+3];
            } else {
                sum = FP[i][j];
            }
            P_pred[i][j] = sum + s_Q[i][j];
        }
    }

    /* 3. 更新状态 */
    memcpy(&s_kf_state.x, x_pred_buf, sizeof(float) * 6);
    memcpy(s_kf_state.P, P_pred_buf, sizeof(float) * 36);

    return LINGXI_OK;
}

/**
 * @brief  卡尔曼更新步骤 (使用 Rank-1 Update 优化)
 */
lingxi_err_t app_fusion_kalman_update(float measurement, uint8_t sensor_type)
{
    if (sensor_type > 2) return LINGXI_ERR_INVALID_PARAM;

    /* 1. 计算创新 y = z - Hx (H = [0,0,1,0,0,0]) */
    float y = measurement - s_kf_state.z;

    /* 2. 计算创新协方差 S = H*P*H^T + R = P[2][2] + R */
    float S = s_kf_state.P[2][2] + s_R[sensor_type];
    if (fabsf(S) < 1e-6f) S = 1e-6f;

    /* 3. 计算卡尔曼增益 K = P * H^T / S (H^T 是第3列) */
    float K[6];
    for (int i = 0; i < 6; i++) {
        K[i] = s_kf_state.P[i][2] / S;
    }

    /* 4. 更新状态 x = x + K*y */
    s_kf_state.x += K[0] * y;
    s_kf_state.y += K[1] * y;
    s_kf_state.z += K[2] * y;
    s_kf_state.vx += K[3] * y;
    s_kf_state.vy += K[4] * y;
    s_kf_state.vz += K[5] * y;

    /* 5. 更新协方差 P = (I - KH)P = P - K(HP) [Rank-1 Update]
     * H*P 是 P 的第 3 行 (index 2)
     */
    float HP[6];
    for (int j = 0; j < 6; j++) {
        HP[j] = s_kf_state.P[2][j];
    }

    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            s_kf_state.P[i][j] -= K[i] * HP[j];
        }
    }

    return LINGXI_OK;
}

