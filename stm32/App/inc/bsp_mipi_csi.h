/**
  ******************************************************************************
  * @file    bsp_mipi_csi.h
  * @brief   MIPI CSI-2 驱动头文件 (VD55G1, 单Lane)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 支持 640x480@60fps, 单Lane, RAW10/RAW8
  * - 使用 STM32N6 DCMIPP + MIPI CSI-2 外设
  * - DMA 双缓冲传输到 SDRAM
  ******************************************************************************
  */

#ifndef __BSP_MIPI_CSI_H
#define __BSP_MIPI_CSI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * MIPI CSI-2 配置参数
 * ==========================================================================*/
#define MIPI_CSI_LANE_COUNT         1       /* 单Lane */
#define MIPI_CSI_DATA_TYPE          0x2B    /* RAW10 */
#define MIPI_CSI_DATA_TYPE_ALT      0x2A    /* RAW8  */

/* 图像分辨率 */
#define CAM_WIDTH                   640
#define CAM_HEIGHT                  480
#define CAM_FPS                     60
#define CAM_BPP                     2       /* RAW10 packed = 2 bytes/pixel approx */
#define CAM_FRAME_SIZE              (CAM_WIDTH * CAM_HEIGHT * CAM_BPP)

/* 双缓冲配置 */
#define CAM_BUFFER_COUNT            2
#define CAM_BUFFER_SIZE             CAM_FRAME_SIZE
#define CAM_BUFFER_ALIGN            32      /* Cache line alignment */

/* MIPI CSI-2 时钟 (Mbps per lane) */
#define MIPI_CSI_CLK_MBPS           800     /* 800 Mbps, 满足 640x480@60fps RAW10 */

/* =============================================================================
 * 数据结构
 * ==========================================================================*/

typedef enum {
    CAM_FMT_RAW8  = 0,
    CAM_FMT_RAW10 = 1,
    CAM_FMT_RAW12 = 2,
    CAM_FMT_YUV422 = 3,
} cam_format_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    cam_format_t format;
    uint32_t fps;
    uint32_t buffer_addr[CAM_BUFFER_COUNT];
    uint8_t  buffer_idx;
} cam_config_t;

typedef struct {
    uint32_t frame_count;
    uint32_t drop_count;
    uint32_t err_count;
    uint32_t last_frame_time_ms;
    uint32_t avg_fps;
} cam_stats_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 MIPI CSI-2 接口和 DCMIPP
 * @param  config: 摄像头配置参数
 * @retval LINGXI_OK 成功, 其他错误码
 */
lingxi_err_t bsp_mipi_csi_init(const cam_config_t *config);

/**
 * @brief  启动摄像头数据采集
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_mipi_csi_start(void);

/**
 * @brief  停止摄像头数据采集
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_mipi_csi_stop(void);

/**
 * @brief  获取当前帧缓冲区地址
 * @retval 缓冲区地址 (SDRAM), NULL 表示无新帧
 */
uint8_t* bsp_mipi_csi_get_frame(void);

/**
 * @brief  释放帧缓冲区
 * @param  frame: 帧缓冲区指针
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_mipi_csi_release_frame(uint8_t *frame);

/**
 * @brief  获取摄像头统计信息
 * @param  stats: 统计信息结构体指针
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_mipi_csi_get_stats(cam_stats_t *stats);

/**
 * @brief  MIPI CSI-2 中断服务程序 (ISR)
 * @note   在 stm32n6xx_it.c 中调用
 */
void bsp_mipi_csi_isr(void);

/**
 * @brief  DCMIPP DMA 传输完成回调
 * @note   在 HAL 回调中调用
 */
void bsp_mipi_csi_dma_tc_callback(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_MIPI_CSI_H */
