/**
  ******************************************************************************
  * @file    app_camera.c
  * @brief   相机应用层实现 — 帧捕获流水线
  * @author  Lingxi Team
  * @version v4.0
  * @date    2026-05-17
  ******************************************************************************
  * 流水线:
  *   VD55G1 → MIPI CSI-2 → DCMIPP → SDRAM (双缓冲)
  *       ↓
  *   bsp_mipi_csi_get_frame() → app_camera_process_frame()
  *       ↓
  *   下采样 640×480 → 320×240 (水平+垂直 2:1)
  *       ↓
  *   帧就绪回调 → JPEG 压缩 / SDIO 发送
  ******************************************************************************
  */

#include "app_camera.h"
#include "bsp_mipi_csi.h"
#include <string.h>

/* =============================================================================
 * 私有宏
 * ==========================================================================*/
#define DWT_DELAY_MS(ms)    do { uint32_t start = DWT->CYCCNT; \
    while ((DWT->CYCCNT - start) < ((SystemCoreClock / 1000) * (ms))); } while(0)

#define CLAMP(x, lo, hi)    (((x) < (lo)) ? (lo) : (((x) > (hi)) ? (hi) : (x)))

/* =============================================================================
 * 私有数据类型
 * ==========================================================================*/
typedef struct {
    /* 配置 */
    uint16_t    out_w;
    uint16_t    out_h;
    uint8_t     target_fps;
    uint32_t    min_frame_ticks;    /* DWT ticks 最小帧间隔 */

    /* 三缓冲：0=DCMIPP写入, 1=正在处理, 2=就绪待回调 */
    uint8_t     out_buf[APP_CAM_BUF_COUNT][APP_CAM_OUT_SIZE] __attribute__((aligned(32)));
    volatile uint8_t  buf_state[APP_CAM_BUF_COUNT];  /* 0=空闲, 1=处理中, 2=就绪 */
    uint8_t     active_buf;          /* 当前正在累积的缓冲索引 */

    /* 回调 */
    app_cam_frame_cb_t callback;
    void                *cb_userdata;

    /* 统计 */
    app_cam_stats_t stats;
    volatile uint8_t     running;
} app_cam_ctx_t;

static app_cam_ctx_t s_ctx = {0};

/* =============================================================================
 * 下采样: 640×480 RAW8 → 320×240 RAW8 (简单 2×2 平均)
 * ==========================================================================*/
static void downsample_2x(const uint8_t *src, uint8_t *dst,
                          uint16_t src_w, uint16_t src_h)
{
    const uint16_t dst_w = src_w >> 1;
    const uint16_t dst_h = src_h >> 1;

    for (uint16_t y = 0; y < dst_h; y++) {
        for (uint16_t x = 0; x < dst_w; x++) {
            const uint16_t sx = x << 1;
            const uint16_t sy = y << 1;
            uint32_t sum = 0;
            sum += src[sy * src_w + sx];
            sum += src[sy * src_w + sx + 1];
            sum += src[(sy + 1) * src_w + sx];
            sum += src[(sy + 1) * src_w + sx + 1];
            dst[y * dst_w + x] = (uint8_t)(sum >> 2);
        }
    }
}

/* =============================================================================
 * API 实现
 * ==========================================================================*/
lingxi_err_t app_camera_init(uint16_t out_w, uint16_t out_h, uint8_t fps)
{
    if (out_w == 0 || out_h == 0 || fps == 0 || fps > APP_CAM_MAX_FPS) {
        return LINGXI_ERR_INVALID_PARAM;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.out_w       = out_w;
    s_ctx.out_h       = out_h;
    s_ctx.target_fps  = fps;

    /* 计算最小帧间隔 (DWT CYCCNT ticks) */
    s_ctx.min_frame_ticks = (SystemCoreClock / fps);

    /* 初始化 BSP 层 */
    cam_config_t cam_cfg = {
        .width  = APP_CAM_FULL_W,
        .height = APP_CAM_FULL_H,
        .format = CAM_FMT_RAW8,
        .fps    = CAM_FPS,       /* 传感器以最高帧率运行 */
    };

    lingxi_err_t err = bsp_mipi_csi_init(&cam_cfg);
    if (err != LINGXI_OK) {
        return err;
    }

    s_ctx.running = 0;
    return LINGXI_OK;
}

void app_camera_register_callback(app_cam_frame_cb_t cb, void *userdata)
{
    s_ctx.callback    = cb;
    s_ctx.cb_userdata = userdata;
}

lingxi_err_t app_camera_start(void)
{
    if (s_ctx.running) return LINGXI_OK;

    lingxi_err_t err = bsp_mipi_csi_start();
    if (err != LINGXI_OK) return err;

    s_ctx.running = 1;
    return LINGXI_OK;
}

lingxi_err_t app_camera_stop(void)
{
    s_ctx.running = 0;
    return bsp_mipi_csi_stop();
}

void app_camera_process_frame(void)
{
    if (!s_ctx.running) return;

    uint32_t t_start = DWT->CYCCNT;

    /* 1. 从 BSP 获取新帧 */
    uint8_t *raw_frame = bsp_mipi_csi_get_frame();
    if (raw_frame == NULL) {
        return;  /* 无新帧 */
    }

    /* 2. 帧率控制: 检查最小间隔 */
    uint32_t elapsed = DWT->CYCCNT - s_ctx.stats.last_frame_ts;
    if (elapsed < s_ctx.min_frame_ticks) {
        /* 帧率过快，丢弃此帧 */
        bsp_mipi_csi_release_frame(raw_frame);
        s_ctx.stats.frames_dropped++;
        return;
    }

    /* 3. 找空闲输出缓冲 */
    uint8_t buf_idx;
    for (buf_idx = 0; buf_idx < APP_CAM_BUF_COUNT; buf_idx++) {
        if (s_ctx.buf_state[buf_idx] == 0) break;
    }
    if (buf_idx >= APP_CAM_BUF_COUNT) {
        /* 所有缓冲忙，丢弃 */
        bsp_mipi_csi_release_frame(raw_frame);
        s_ctx.stats.frames_dropped++;
        return;
    }

    /* 4. 下采样: 640×480 → out_w×out_h */
    s_ctx.buf_state[buf_idx] = 1;
    downsample_2x(raw_frame, s_ctx.out_buf[buf_idx],
                  APP_CAM_FULL_W, APP_CAM_FULL_H);

    /* 释放 BSP 帧缓冲区 */
    bsp_mipi_csi_release_frame(raw_frame);

    /* 5. 标记就绪，记录时间戳 */
    s_ctx.buf_state[buf_idx] = 2;
    s_ctx.stats.last_frame_ts = t_start;
    s_ctx.stats.frames_captured++;

    /* 6. 如果注册了回调，调用它 */
    if (s_ctx.callback) {
        uint64_t timestamp_us = (uint64_t)(DWT->CYCCNT / (SystemCoreClock / 1000000));
        s_ctx.callback(s_ctx.out_buf[buf_idx], s_ctx.out_w * s_ctx.out_h,
                       timestamp_us, s_ctx.cb_userdata);
        s_ctx.stats.frames_sent++;
    }

    /* 7. 缓冲状态回空闲 */
    s_ctx.buf_state[buf_idx] = 0;

    /* 8. 更新处理耗时统计 */
    uint32_t t_delta = DWT->CYCCNT - t_start;
    uint32_t us = t_delta / (SystemCoreClock / 1000000);
    /* 滑动平均 */
    s_ctx.stats.avg_process_us = (s_ctx.stats.avg_process_us + us) >> 1;
}

lingxi_err_t app_camera_get_stats(app_cam_stats_t *stats)
{
    if (stats == NULL) return LINGXI_ERR_NULL_PTR;
    memcpy(stats, &s_ctx.stats, sizeof(app_cam_stats_t));

    /* 计算实际帧率 */
    stats->actual_fps = s_ctx.stats.frames_captured /
                        (uint32_t)((HAL_GetTick() + 1) / 1000);
    return LINGXI_OK;
}
