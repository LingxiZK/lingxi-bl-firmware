/**
  ******************************************************************************
  * @file    app_camera.h
  * @brief   相机应用层接口 — 帧捕获 + 下采样 + 时间戳
  * @author  Lingxi Team
  * @version v4.0
  * @date    2026-05-17
  ******************************************************************************
  * @attention
  * - 封装 bsp_mipi_csi 为应用层服务
  * - 提供帧就绪回调机制
  * - 支持降采样 640x480 → 320x240 用于传输
  * - 统一时间戳 (DWT_CYCCNT)
  ******************************************************************************
  */

#ifndef __APP_CAMERA_H
#define __APP_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * 相机配置
 * ==========================================================================*/
#define APP_CAM_FULL_W             640     /* 传感器原生宽度 */
#define APP_CAM_FULL_H             480     /* 传感器原生高度 */
#define APP_CAM_FULL_SIZE          (APP_CAM_FULL_W * APP_CAM_FULL_H)  /* RAW8 单字节 */

#define APP_CAM_OUT_W              320     /* 输出宽度（降采样） */
#define APP_CAM_OUT_H              240     /* 输出高度 */
#define APP_CAM_OUT_SIZE           (APP_CAM_OUT_W * APP_CAM_OUT_H)

#define APP_CAM_MAX_FPS            30      /* 输出帧率上限 */
#define APP_CAM_BUF_COUNT          3       /* 三缓冲：采集/处理/发送 */

/* 回调函数类型 */
typedef void (*app_cam_frame_cb_t)(const uint8_t *frame, uint32_t size,
                                    uint64_t timestamp_us, void *userdata);

/* =============================================================================
 * API
 * ==========================================================================*/

/**
 * @brief  初始化相机应用层
 * @param  out_w / out_h: 输出分辨率（支持降采样）
 * @param  fps: 目标帧率
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_camera_init(uint16_t out_w, uint16_t out_h, uint8_t fps);

/**
 * @brief  注册帧就绪回调（在 vTaskCamera 中调用）
 * @param  cb: 回调函数（见 app_cam_frame_cb_t）
 * @param  userdata: 透传用户数据
 */
void app_camera_register_callback(app_cam_frame_cb_t cb, void *userdata);

/**
 * @brief  启动帧捕获
 */
lingxi_err_t app_camera_start(void);

/**
 * @brief  停止帧捕获
 */
lingxi_err_t app_camera_stop(void);

/**
 * @brief  vTaskCamera 主循环 — 每帧调用一次
 * @note   由 FreeRTOS vTaskCamera 调用，非线程安全
 */
void app_camera_process_frame(void);

/**
 * @brief  获取相机状态
 */
typedef struct {
    uint32_t frames_captured;
    uint32_t frames_sent;
    uint32_t frames_dropped;
    uint32_t avg_process_us;       /* 每帧处理耗时均值 (μs) */
    float    actual_fps;
    uint64_t last_frame_ts;        /* 最后一帧时间戳 (DWT ticks) */
} app_cam_stats_t;

lingxi_err_t app_camera_get_stats(app_cam_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CAMERA_H */
