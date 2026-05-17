/**
 ******************************************************************************
 * @file    app_control.c
 * @brief   主控逻辑实现 (避障/边缘/跟踪) - PID + 前馈 + 指令平滑
 * @author  Lingxi Team
 * @version v4.0
 * @date    2026-05-17
 ******************************************************************************
 * @attention
 * - 三段式 PID (Z轴/Yaw/速度) 独立控制，带积分抗饱和 (Anti-windup)
 * - 前馈 (Feedforward) 补偿惯性滞后，提升响应速度
 * - 指令平滑 (Slew Rate Limiter) 抑制电机高频抖动
 * - 模式状态机：IDLE ↔ OBSTACLE_AVOID ↔ EDGE_DETECT ↔ TRACK ↔ EMERGENCY
 ******************************************************************************
 */

#include "app_control.h"
#include <string.h>
#include <math.h>
#include "stm32n6xx_hal.h"

/* =============================================================================
 * 私有类型定义
 * ==========================================================================*/

/**
 * @brief PID 控制器实例
 */
typedef struct {
    /* 误差累积 */
    float integral;          /* 积分项 (受 anti-windup 保护) */
    float prev_error;        /* 上次误差 (用于微分) */
    float prev_output;       /* 上次输出 (用于前馈记忆) */

    /* 参数 */
    float Kp;                /* 比例增益 */
    float Ki;                /* 积分增益 */
    float Kd;                /* 微分增益 */
    float ff_gain;           /* 前馈增益 (feedforward) */
    float integral_limit;    /* 积分限幅 (anti-windup) */
    float output_limit;      /* 输出限幅 */

    /* 指令平滑 (Slew Rate Limiter) */
    float slew_rate;         /* 最大变化率 (单位/秒) */
    float last_slew_output;  /* 上次平滑后的输出 */
    uint32_t last_update_ms; /* 上次更新时间戳 */

    /* 统计 */
    uint32_t update_count;
} pid_ctrl_t;

/* =============================================================================
 * 私有变量
 * ==========================================================================*/

static uint8_t s_control_initialized = 0;
static control_state_t s_state = {0};

/* 三轴 PID 控制器 */
static pid_ctrl_t s_z_pid;      /* 高度/距离控制 (Z轴) */
static pid_ctrl_t s_yaw_pid;    /* 航向控制 (Yaw) */
static pid_ctrl_t s_speed_pid;  /* 水平速度控制 (X/Y) */

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/

static void pid_init(pid_ctrl_t *pid, float Kp, float Ki, float Kd,
                     float ff_gain, float integral_limit,
                     float output_limit, float slew_rate);
static float pid_update(pid_ctrl_t *pid, float setpoint, float measurement,
                        uint32_t timestamp_ms);
static float pid_apply_slew(pid_ctrl_t *pid, float raw_output,
                            uint32_t timestamp_ms);

static lingxi_err_t handle_obstacle_avoid(control_state_t *state,
                                          const npu_infer_result_t *infer,
                                          const sensor_fusion_data_t *sensor);
static lingxi_err_t handle_edge_detect(control_state_t *state,
                                       const npu_infer_result_t *infer);
static lingxi_err_t handle_track(control_state_t *state,
                                 const npu_infer_result_t *infer);
static void handle_emergency(control_state_t *state);
static void smooth_mode_transition(control_state_t *state,
                                   control_mode_t new_mode,
                                   uint32_t timestamp_ms);

/* =============================================================================
 * API 实现
 * ==========================================================================*/

/**
 * @brief  初始化主控逻辑模块
 */
lingxi_err_t app_control_init(void)
{
    memset(&s_state, 0, sizeof(control_state_t));
    s_state.current_mode = CTRL_MODE_IDLE;
    s_state.last_mode = CTRL_MODE_IDLE;

    /* 初始化三轴 PID 控制器 */
    /* Z轴 (高度/距离): 慢响应, 大阻尼 */
    pid_init(&s_z_pid,
             2.5f,     /* Kp */
             0.15f,    /* Ki */
             0.8f,     /* Kd */
             0.1f,     /* ff_gain */
             500.0f,   /* integral_limit */
             2000.0f,  /* output_limit (mm/s) */
             500.0f);  /* slew_rate (mm/s/s) */

    /* Yaw (航向): 快响应, 低阻尼 */
    pid_init(&s_yaw_pid,
             8.0f,     /* Kp */
             0.05f,    /* Ki */
             3.0f,     /* Kd */
             0.0f,     /* ff_gain */
             200.0f,   /* integral_limit */
             300.0f,   /* output_limit (deg/s) */
             180.0f);  /* slew_rate (deg/s/s) */

    /* 水平速度: 中等响应 */
    pid_init(&s_speed_pid,
             1.8f,     /* Kp */
             0.10f,    /* Ki */
             0.5f,     /* Kd */
             0.15f,    /* ff_gain */
             300.0f,   /* integral_limit */
             1000.0f,  /* output_limit (mm/s) */
             400.0f);  /* slew_rate (mm/s/s) */

    s_control_initialized = 1;

    LX_DEBUG_PRINT("Control module initialized (PID+FF+Slew)");
    return LINGXI_OK;
}

/**
 * @brief  更新控制逻辑 (主入口)
 * @note   由 vTaskControl 每 20ms (50Hz) 调用
 */
lingxi_err_t app_control_update(control_state_t *state,
                                const npu_infer_result_t *infer_result,
                                const sensor_fusion_data_t *sensor_data)
{
    LX_RETURN_IF_NULL(state);
    LX_RETURN_IF_NULL(infer_result);
    LX_RETURN_IF_NULL(sensor_data);
    if (!s_control_initialized) return LINGXI_ERR_NOT_INIT;

    uint32_t now = sensor_data->timestamp_ms;
    if (now == 0) {
        now = HAL_GetTick();
    }

    /* 更新内部状态 */
    s_state.last_update_ms = now;

    /* ================================================================
     * 模式选择与状态转移
     * ================================================================ */

    control_mode_t target_mode = s_state.current_mode;

    /* 1. 紧急停止优先级最高 */
    if (infer_result->obstacle_detected && 
        sensor_data->tof_valid && 
        sensor_data->tof_distance_mm < OBSTACLE_CRITICAL_DISTANCE_MM) {
        target_mode = CTRL_MODE_EMERGENCY;
    }
    /* 2. 避障 (有障碍物但未到临界距离) */
    else if (infer_result->obstacle_detected &&
             sensor_data->fused_distance_mm < OBSTACLE_SAFE_DISTANCE_MM) {
        target_mode = CTRL_MODE_OBSTACLE_AVOID;
    }
    /* 3. 边缘检测 */
    else if (infer_result->edge_detected) {
        target_mode = CTRL_MODE_EDGE_DETECT;
    }
    /* 4. 目标跟踪 */
    else if (infer_result->track_target_valid) {
        target_mode = CTRL_MODE_TRACK;
    }
    /* 5. 空闲 / 默认 */
    else {
        target_mode = CTRL_MODE_IDLE;
    }

    /* 执行模式切换 (带平滑过渡) */
    if (target_mode != s_state.current_mode) {
        smooth_mode_transition(&s_state, target_mode, now);
    }

    /* ================================================================
     * 按当前模式执行控制逻辑
     * ================================================================ */

    lingxi_err_t err = LINGXI_OK;

    switch (s_state.current_mode) {
        case CTRL_MODE_EMERGENCY:
            handle_emergency(&s_state);
            break;

        case CTRL_MODE_OBSTACLE_AVOID:
            err = handle_obstacle_avoid(&s_state, infer_result, sensor_data);
            break;

        case CTRL_MODE_EDGE_DETECT:
            err = handle_edge_detect(&s_state, infer_result);
            break;

        case CTRL_MODE_TRACK:
            err = handle_track(&s_state, infer_result);
            break;

        case CTRL_MODE_IDLE:
        default:
            /* 空闲模式: 逐步减速到零 */
            s_state.target_distance_mm = 0;
            s_state.target_heading_deg = 0;
            s_state.target_speed_mmps = 0;
            break;
    }

    /* 复制状态到输出 */
    memcpy(state, &s_state, sizeof(control_state_t));
    *state = s_state;

    return err;
}

/**
 * @brief  避障决策
 */
lingxi_err_t app_control_obstacle_avoid(control_state_t *state,
                                         const npu_infer_result_t *infer_result,
                                         const sensor_fusion_data_t *sensor_data)
{
    LX_RETURN_IF_NULL(state);
    LX_RETURN_IF_NULL(infer_result);
    LX_RETURN_IF_NULL(sensor_data);

    smooth_mode_transition(&s_state, CTRL_MODE_OBSTACLE_AVOID, 
                          (sensor_data->timestamp_ms != 0) ? 
                           sensor_data->timestamp_ms : HAL_GetTick());

    lingxi_err_t err = handle_obstacle_avoid(&s_state, infer_result, sensor_data);
    *state = s_state;
    return err;
}

/**
 * @brief  边缘识别决策
 */
lingxi_err_t app_control_edge_detect(control_state_t *state,
                                     const npu_infer_result_t *infer_result)
{
    LX_RETURN_IF_NULL(state);
    LX_RETURN_IF_NULL(infer_result);

    smooth_mode_transition(&s_state, CTRL_MODE_EDGE_DETECT,
                          HAL_GetTick());

    lingxi_err_t err = handle_edge_detect(&s_state, infer_result);
    *state = s_state;
    return err;
}

/**
 * @brief  目标跟踪决策
 */
lingxi_err_t app_control_track(control_state_t *state,
                               const npu_infer_result_t *infer_result)
{
    LX_RETURN_IF_NULL(state);
    LX_RETURN_IF_NULL(infer_result);

    smooth_mode_transition(&s_state, CTRL_MODE_TRACK,
                          HAL_GetTick());

    lingxi_err_t err = handle_track(&s_state, infer_result);
    *state = s_state;
    return err;
}

/**
 * @brief  紧急停止
 */
lingxi_err_t app_control_emergency_stop(control_state_t *state)
{
    LX_RETURN_IF_NULL(state);

    smooth_mode_transition(&s_state, CTRL_MODE_EMERGENCY,
                          HAL_GetTick());

    handle_emergency(&s_state);
    *state = s_state;
    return LINGXI_OK;
}

/* =============================================================================
 * PID 控制器实现
 * ==========================================================================*/

/**
 * @brief  PID 控制器初始化
 */
static void pid_init(pid_ctrl_t *pid, float Kp, float Ki, float Kd,
                     float ff_gain, float integral_limit,
                     float output_limit, float slew_rate)
{
    memset(pid, 0, sizeof(pid_ctrl_t));

    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->ff_gain = ff_gain;
    pid->integral_limit = integral_limit;
    pid->output_limit = output_limit;
    pid->slew_rate = slew_rate;
}

/**
 * @brief  PID 更新 (位置式 PID + 前馈)
 * @note   算法: u = Kp*e + Ki*∫e + Kd*Δe/Δt + ff * setpoint_rate
 *         - 积分项带 anti-windup: 积分饱和时停止积分累积
 *         - 微分项使用 "微分先行" (只对测量值微分, 不对设定值微分)
 *           避免设定值突变引起的微分激增 (Derivative Kick)
 *         - 前馈项根据设定值变化率补偿惯性
 */
static float pid_update(pid_ctrl_t *pid, float setpoint, float measurement,
                        uint32_t timestamp_ms)
{
    float error = setpoint - measurement;
    float dt = 0.001f;  /* 默认 1ms */

    /* 计算实际 dt (防止 dt=0) */
    if (pid->last_update_ms != 0 && timestamp_ms > pid->last_update_ms) {
        dt = (float)(timestamp_ms - pid->last_update_ms) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;   /* 限幅: 最大 100ms */
    }

    /* === P项 === */
    float p_term = pid->Kp * error;

    /* === I项 (带 Anti-windup) === */
    pid->integral += error * dt;
    /* Anti-windup: 钳制积分 */
    if (pid->integral > pid->integral_limit) {
        pid->integral = pid->integral_limit;
    } else if (pid->integral < -pid->integral_limit) {
        pid->integral = -pid->integral_limit;
    }
    float i_term = pid->Ki * pid->integral;

    /* === D项 (微分先行: 只对测量值微分, 避免 Deriative Kick) === */
    float d_term = 0.0f;
    if (dt > 1e-6f) {
        float measurement_change = (measurement - pid->prev_output);
        d_term = pid->Kd * (-measurement_change / dt);
    }

    /* 保存当前测量值用于下次微分 */
    pid->prev_output = measurement;
    pid->prev_error = error;

    /* === 前馈项 (Feedforward) === */
    /* 根据设定值变化率补偿惯性滞后 */
    static float prev_setpoint = 0.0f;
    float ff_term = 0.0f;
    if (pid->ff_gain > 0.0f && dt > 1e-6f) {
        float setpoint_rate = (setpoint - prev_setpoint) / dt;
        ff_term = pid->ff_gain * setpoint_rate;
    }
    prev_setpoint = setpoint;

    /* === 总输出 === */
    float output = p_term + i_term + d_term + ff_term;

    /* 输出限幅 */
    if (output > pid->output_limit) {
        output = pid->output_limit;
    } else if (output < -pid->output_limit) {
        output = -pid->output_limit;
    }

    pid->update_count++;
    pid->last_update_ms = timestamp_ms;

    return output;
}

/**
 * @brief  指令平滑 (Slew Rate Limiter)
 * @note   限制输出变化率，防止指令突变导致电机抖动
 */
static float pid_apply_slew(pid_ctrl_t *pid, float raw_output,
                            uint32_t timestamp_ms)
{
    if (pid->slew_rate <= 0.0f) {
        pid->last_slew_output = raw_output;
        return raw_output;
    }

    float dt = 0.001f;
    if (pid->last_update_ms != 0 && timestamp_ms > pid->last_update_ms) {
        dt = (float)(timestamp_ms - pid->last_update_ms) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
    }

    float max_change = pid->slew_rate * dt;
    float delta = raw_output - pid->last_slew_output;

    if (delta > max_change) {
        pid->last_slew_output += max_change;
    } else if (delta < -max_change) {
        pid->last_slew_output -= max_change;
    } else {
        pid->last_slew_output = raw_output;
    }

    return pid->last_slew_output;
}

/* =============================================================================
 * 模式处理函数
 * ==========================================================================*/

/**
 * @brief  避障处理
 * @note   根据 ToF 距离 + 视觉检测，通过 PID 控制速度和航向
 */
static lingxi_err_t handle_obstacle_avoid(control_state_t *state,
                                          const npu_infer_result_t *infer,
                                          const sensor_fusion_data_t *sensor)
{
    float fused_dist = sensor->fused_distance_mm;
    uint32_t now = sensor->timestamp_ms;

    if (now == 0) now = HAL_GetTick();

    /* 安全距离值映射: 距离越近, 减速幅度越大 */
    float safe_dist_cmd = 0.0f;

    if (fused_dist < OBSTACLE_CRITICAL_DISTANCE_MM) {
        /* 临界距离: 全速后退 */
        safe_dist_cmd = -OBSTACLE_AVOID_SPEED_MMPS;
    } else if (fused_dist < OBSTACLE_WARNING_DISTANCE_MM) {
        /* 警告距离: 线性减速 + 转向 */
        float ratio = (fused_dist - OBSTACLE_CRITICAL_DISTANCE_MM) /
                      (OBSTACLE_WARNING_DISTANCE_MM - OBSTACLE_CRITICAL_DISTANCE_MM);
        safe_dist_cmd = -OBSTACLE_AVOID_SPEED_MMPS * (1.0f - ratio);
    } else if (fused_dist < OBSTACLE_SAFE_DISTANCE_MM) {
        /* 安全距离内: 缓慢接近 */
        safe_dist_cmd = OBSTACLE_AVOID_SPEED_MMPS * 0.3f;
    } else {
        /* 安全距离外: 正常巡航 */
        safe_dist_cmd = OBSTACLE_AVOID_SPEED_MMPS * 0.6f;
    }

    /* PID 速度控制 */
    float speed_raw = pid_update(&s_speed_pid, safe_dist_cmd,
                                 sensor->fused_distance_mm, now);
    float speed_smooth = pid_apply_slew(&s_speed_pid, speed_raw, now);

    /* 航向角偏差: 检测到障碍物的边沿方向 */
    float heading_error = 0.0f;
    if (infer->num_detections > 0) {
        /* 计算障碍物相对于图像中心的偏移 */
        float obj_center_x = (float)infer->detections[0].x +
                            (float)infer->detections[0].w / 2.0f;
        float image_center_x = NPU_INPUT_WIDTH / 2.0f;
        /* 偏移量映射到航向角 (简单比例) */
        heading_error = (obj_center_x - image_center_x) / image_center_x * 30.0f;
    }

    float yaw_raw = pid_update(&s_yaw_pid, heading_error, 0.0f, now);
    float yaw_smooth = pid_apply_slew(&s_yaw_pid, yaw_raw, now);

    /* 更新状态 */
    state->target_distance_mm = (uint16_t)(fused_dist);
    state->target_heading_deg = (int16_t)(yaw_smooth);
    state->target_speed_mmps = (int16_t)(speed_smooth);
    state->emergency_stop = 0;

    return LINGXI_OK;
}

/**
 * @brief  边缘识别处理
 * @note   检测到飞行平台边缘 (如桌面/楼顶边缘) 时, 保持悬停并发出警告
 */
static lingxi_err_t handle_edge_detect(control_state_t *state,
                                       const npu_infer_result_t *infer)
{
    uint32_t now = HAL_GetTick();

    /* 边缘检测: 立即制动, 保持悬停 */
    float speed_raw = pid_update(&s_speed_pid, 0.0f, 0.0f, now);
    float speed_smooth = pid_apply_slew(&s_speed_pid, speed_raw, now);

    /* 航向: 根据边缘在图像中的位置, 轻微转向 */
    float heading_target = 0.0f;
    if (infer->num_detections > 0) {
        float edge_center_x = (float)infer->detections[0].x +
                              (float)infer->detections[0].w / 2.0f;
        float image_center_x = NPU_INPUT_WIDTH / 2.0f;
        heading_target = (edge_center_x - image_center_x) / image_center_x * 45.0f;
    }

    float yaw_raw = pid_update(&s_yaw_pid, heading_target, 0.0f, now);
    float yaw_smooth = pid_apply_slew(&s_yaw_pid, yaw_raw, now);

    state->target_distance_mm = 0;
    state->target_heading_deg = (int16_t)(yaw_smooth);
    state->target_speed_mmps = (int16_t)(speed_smooth);
    state->emergency_stop = 0;

    return LINGXI_OK;
}

/**
 * @brief  目标跟踪处理
 * @note   根据 NPU 跟踪结果 (track_x, track_y) 生成跟踪指令
 */
static lingxi_err_t handle_track(control_state_t *state,
                                 const npu_infer_result_t *infer)
{
    uint32_t now = HAL_GetTick();

    /* 跟踪目标位置偏差 */
    float track_dx = infer->track_x - (NPU_INPUT_WIDTH / 2.0f);
    float track_dy = infer->track_y - (NPU_INPUT_HEIGHT / 2.0f);
    (void)(track_dy); /* 预留垂直偏差, 后续扩展使用 */

    /* 偏差映射到控制量 */
    float yaw_target = track_dx / (NPU_INPUT_WIDTH / 2.0f) * 30.0f;

    /* 根据目标大小估算距离 (目标越大越近) */
    float dist_estimation = 0.0f;
    if (infer->num_detections > 0) {
        float obj_area = (float)infer->detections[0].w * 
                         (float)infer->detections[0].h;
        float ref_area = (float)(NPU_INPUT_WIDTH/4) * (float)(NPU_INPUT_HEIGHT/4);
        if (obj_area > 0.0f) {
            dist_estimation = ref_area / obj_area * 1000.0f;
        }
    }

    /* PID 控制 */
    float yaw_raw = pid_update(&s_yaw_pid, yaw_target, 0.0f, now);
    float yaw_smooth = pid_apply_slew(&s_yaw_pid, yaw_raw, now);

    float speed_raw = pid_update(&s_speed_pid, dist_estimation, 0.0f, now);
    float speed_smooth = pid_apply_slew(&s_speed_pid, speed_raw, now);

    state->target_distance_mm = (uint16_t)(dist_estimation);
    state->target_heading_deg = (int16_t)(yaw_smooth);
    state->target_speed_mmps = (int16_t)(speed_smooth);
    state->emergency_stop = 0;

    return LINGXI_OK;
}

/**
 * @brief  紧急停止处理
 * @note   立即制动, 等待上位机恢复指令
 */
static void handle_emergency(control_state_t *state)
{
    /* 复位 PID 积分器 */
    memset(&s_z_pid, 0, offsetof(pid_ctrl_t, Kp));
    memset(&s_yaw_pid, 0, offsetof(pid_ctrl_t, Kp));
    memset(&s_speed_pid, 0, offsetof(pid_ctrl_t, Kp));
    /* 重新设置 PID 系数 (memset 清空了) */
    s_z_pid.Kp = 2.5f; s_z_pid.Ki = 0.15f; s_z_pid.Kd = 0.8f;
    s_yaw_pid.Kp = 8.0f; s_yaw_pid.Ki = 0.05f; s_yaw_pid.Kd = 3.0f;
    s_speed_pid.Kp = 1.8f; s_speed_pid.Ki = 0.10f; s_speed_pid.Kd = 0.5f;

    state->target_distance_mm = 0;
    state->target_heading_deg = 0;
    state->target_speed_mmps = 0;
    state->emergency_stop = 1;

    LX_DEBUG_PRINT("EMERGENCY STOP ACTIVATED");
}

/**
 * @brief  模式平滑切换
 * @note   记录模式进入时间, 确保最小停留时间
 */
static void smooth_mode_transition(control_state_t *state,
                                   control_mode_t new_mode,
                                   uint32_t timestamp_ms)
{
    /* 防止高频模式切换: 每种模式最少停留 100ms */
    uint32_t min_stay_ms = 100;
    if (timestamp_ms - state->mode_entry_time_ms < min_stay_ms &&
        state->current_mode != CTRL_MODE_EMERGENCY &&
        new_mode != CTRL_MODE_EMERGENCY) {
        return;
    }

    state->last_mode = state->current_mode;
    state->current_mode = new_mode;
    state->mode_entry_time_ms = timestamp_ms;

    /* 在模式进入时重置 PID 积分, 防止上一模式的积分残留 */
    switch (new_mode) {
        case CTRL_MODE_OBSTACLE_AVOID:
        case CTRL_MODE_EDGE_DETECT:
        case CTRL_MODE_TRACK:
            s_z_pid.integral = 0.0f;
            s_yaw_pid.integral = 0.0f;
            s_speed_pid.integral = 0.0f;
            break;
        default:
            break;
    }

    LX_DEBUG_PRINT("Mode switch: %d -> %d", state->last_mode, state->current_mode);
}
