/**
  ******************************************************************************
  * @file    mipi_csi2.h
  * @brief   MIPI CSI-2 HAL Driver Header (VD55G1, STM32N657)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - Target: STM32N657L0H3Q, DCMIPP + MIPI CSI-2 PHY
  * - Sensor: VD55G1, 640x480@60fps, Single Lane
  * - Data Rate: 500 Mbps per lane
  * - Image Format: YUV422 (0x1E)
  * - Frame Buffer: SDRAM via DMA double-buffering
  * - Pin Mapping: D0P/D0N(PH4/PH5), CLKP/CLKN(PH6/PH7)
  ******************************************************************************
  */

#ifndef __MIPI_CSI2_H
#define __MIPI_CSI2_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * 版本与标识
 * ==========================================================================*/
#define MIPI_CSI2_DRIVER_VERSION    0x0320  /* v3.2.0 */

/* =============================================================================
 * 硬件配置参数
 * ==========================================================================*/
#define MIPI_CSI2_LANE_COUNT        1U          /* 单数据Lane */
#define MIPI_CSI2_DATA_TYPE         0x1EU       /* YUV422 8-bit */
#define MIPI_CSI2_DATA_TYPE_ALT     0x1FU       /* YUV422 10-bit (备用) */

/* 图像参数 */
#define MIPI_CSI2_CAM_WIDTH         640U
#define MIPI_CSI2_CAM_HEIGHT        480U
#define MIPI_CSI2_CAM_FPS           60U
#define MIPI_CSI2_CAM_BPP           2U          /* YUV422 = 2 bytes/pixel */
#define MIPI_CSI2_FRAME_SIZE        (MIPI_CSI2_CAM_WIDTH * MIPI_CSI2_CAM_HEIGHT * MIPI_CSI2_CAM_BPP)

/* 双缓冲 */
#define MIPI_CSI2_BUF_COUNT         2U
#define MIPI_CSI2_BUF_SIZE          MIPI_CSI2_FRAME_SIZE
#define MIPI_CSI2_BUF_ALIGN         32U         /* Cache line对齐 */

/* MIPI CSI-2 PHY 时钟 (Mbps per lane) */
#define MIPI_CSI2_BITRATE_MBPS      500U        /* 500 Mbps */

/* 超时配置 */
#define MIPI_CSI2_INIT_TIMEOUT_MS   1000U
#define MIPI_CSI2_FRAME_TIMEOUT_MS  2000U
#define MIPI_CSI2_STOP_TIMEOUT_MS   500U

/* =============================================================================
 * GPIO 引脚定义 (PH4-PH7)
 * ==========================================================================*/
#define MIPI_CSI2_D0P_PIN           GPIO_PIN_4      /* PH4: CSI_D0+  */
#define MIPI_CSI2_D0N_PIN           GPIO_PIN_5      /* PH5: CSI_D0-  */
#define MIPI_CSI2_CLKP_PIN          GPIO_PIN_6      /* PH6: CSI_CLK+ */
#define MIPI_CSI2_CLKN_PIN          GPIO_PIN_7      /* PH7: CSI_CLK- */
#define MIPI_CSI2_GPIO_PORT         GPIOH
#define MIPI_CSI2_GPIO_AF           GPIO_AF13_DCMIPP

/* =============================================================================
 * 错误码定义
 * ==========================================================================*/
typedef enum {
    MIPI_CSI2_OK = 0,
    MIPI_CSI2_ERR_NULL_PTR = -1,
    MIPI_CSI2_ERR_INVALID_PARAM = -2,
    MIPI_CSI2_ERR_TIMEOUT = -3,
    MIPI_CSI2_ERR_HW_INIT = -4,
    MIPI_CSI2_ERR_DMA = -5,
    MIPI_CSI2_ERR_PHY = -6,
    MIPI_CSI2_ERR_NOT_INIT = -7,
    MIPI_CSI2_ERR_BUSY = -8,
} mipi_csi2_err_t;

/* =============================================================================
 * 图像格式枚举
 * ==========================================================================*/
typedef enum {
    MIPI_CSI2_FMT_YUV422_8  = 0x1E,
    MIPI_CSI2_FMT_YUV422_10 = 0x1F,
    MIPI_CSI2_FMT_RAW8      = 0x2A,
    MIPI_CSI2_FMT_RAW10     = 0x2B,
} mipi_csi2_format_t;

/* =============================================================================
 * 驱动状态枚举
 * ==========================================================================*/
typedef enum {
    MIPI_CSI2_STATE_RESET = 0,
    MIPI_CSI2_STATE_INIT,
    MIPI_CSI2_STATE_READY,
    MIPI_CSI2_STATE_RUNNING,
    MIPI_CSI2_STATE_ERROR,
} mipi_csi2_state_t;

/* =============================================================================
 * 帧缓冲区配置结构体
 * ==========================================================================*/
typedef struct {
    uint32_t buf_addr[MIPI_CSI2_BUF_COUNT]; /* 双缓冲物理地址 */
    uint32_t buf_size;                      /* 单个缓冲区大小 */
    uint8_t  buf_idx;                       /* 当前写入缓冲区索引 */
} mipi_csi2_buffer_t;

/* =============================================================================
 * 捕获配置结构体
 * ==========================================================================*/
typedef struct {
    uint32_t width;                 /* 图像宽度 */
    uint32_t height;                /* 图像高度 */
    mipi_csi2_format_t format;      /* 图像数据类型 */
    uint32_t fps;                   /* 目标帧率 */
    uint32_t bitrate_mbps;          /* MIPI lane bitrate */
    uint8_t  lane_count;            /* 数据lane数量 */
    mipi_csi2_buffer_t buffer;      /* 缓冲区配置 */
} mipi_csi2_config_t;

/* =============================================================================
 * 运行统计结构体
 * ==========================================================================*/
typedef struct {
    uint32_t frame_count;           /* 总帧计数 */
    uint32_t drop_count;            /* 丢帧计数 */
    uint32_t err_count;             /* 错误计数 */
    uint32_t line_err_count;        /* 行错误计数 */
    uint32_t crc_err_count;         /* CRC错误计数 */
    uint32_t last_frame_tick;       /* 上一帧时间戳(ms) */
    uint32_t avg_fps;               /* 平均帧率 */
    uint32_t min_fps;               /* 最小帧率 */
    uint32_t max_fps;               /* 最大帧率 */
} mipi_csi2_stats_t;

/* =============================================================================
 * 中断回调函数类型
 * ==========================================================================*/
typedef void (*mipi_csi2_frame_callback_t)(uint8_t *frame_buf, uint32_t frame_size, void *user_ctx);
typedef void (*mipi_csi2_err_callback_t)(uint32_t err_flags, void *user_ctx);

/* =============================================================================
 * 驱动句柄结构体
 * ==========================================================================*/
typedef struct {
    DCMIPP_HandleTypeDef    *hdcmipp;       /* DCMIPP HAL句柄 */
    DMA_HandleTypeDef       *hdma;          /* DMA HAL句柄 */
    mipi_csi2_state_t       state;          /* 驱动状态 */
    mipi_csi2_config_t      config;         /* 当前配置 */
    mipi_csi2_stats_t       stats;          /* 运行统计 */
    mipi_csi2_frame_callback_t frame_cb;    /* 帧就绪回调 */
    mipi_csi2_err_callback_t   err_cb;    /* 错误回调 */
    void                    *user_ctx;      /* 用户上下文 */
    volatile uint8_t        frame_ready;    /* 新帧标志 */
    volatile uint8_t        dma_busy;       /* DMA忙标志 */
} mipi_csi2_handle_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 MIPI CSI-2 HAL 驱动
 * @param  hcsi: 驱动句柄指针
 * @param  config: 捕获配置参数
 * @retval MIPI_CSI2_OK 成功, 其他错误码
 * @note   必须先配置 SDRAM 缓冲区地址
 */
mipi_csi2_err_t mipi_csi2_init(mipi_csi2_handle_t *hcsi, const mipi_csi2_config_t *config);

/**
 * @brief  反初始化 MIPI CSI-2 HAL 驱动
 * @param  hcsi: 驱动句柄指针
 * @retval MIPI_CSI2_OK 成功
 */
mipi_csi2_err_t mipi_csi2_deinit(mipi_csi2_handle_t *hcsi);

/**
 * @brief  启动图像采集
 * @param  hcsi: 驱动句柄指针
 * @retval MIPI_CSI2_OK 成功
 */
mipi_csi2_err_t mipi_csi2_start(mipi_csi2_handle_t *hcsi);

/**
 * @brief  停止图像采集
 * @param  hcsi: 驱动句柄指针
 * @retval MIPI_CSI2_OK 成功
 */
mipi_csi2_err_t mipi_csi2_stop(mipi_csi2_handle_t *hcsi);

/**
 * @brief  获取当前可读帧缓冲区
 * @param  hcsi: 驱动句柄指针
 * @param  frame_buf: 输出帧缓冲区地址
 * @param  frame_size: 输出帧大小
 * @retval MIPI_CSI2_OK 成功, MIPI_CSI2_ERR_TIMEOUT 无新帧
 */
mipi_csi2_err_t mipi_csi2_get_frame(mipi_csi2_handle_t *hcsi, uint8_t **frame_buf, uint32_t *frame_size);

/**
 * @brief  释放帧缓冲区 (切换到下一帧)
 * @param  hcsi: 驱动句柄指针
 * @retval MIPI_CSI2_OK 成功
 */
mipi_csi2_err_t mipi_csi2_release_frame(mipi_csi2_handle_t *hcsi);

/**
 * @brief  注册帧就绪回调
 * @param  hcsi: 驱动句柄指针
 * @param  callback: 回调函数
 * @param  user_ctx: 用户上下文
 * @retval MIPI_CSI2_OK 成功
 */
mipi_csi2_err_t mipi_csi2_register_frame_callback(mipi_csi2_handle_t *hcsi,
                                                     mipi_csi2_frame_callback_t callback,
                                                     void *user_ctx);

/**
 * @brief  注册错误回调
 * @param  hcsi: 驱动句柄指针
 * @param  callback: 回调函数
 * @param  user_ctx: 用户上下文
 * @retval MIPI_CSI2_OK 成功
 */
mipi_csi2_err_t mipi_csi2_register_err_callback(mipi_csi2_handle_t *hcsi,
                                                 mipi_csi2_err_callback_t callback,
                                                 void *user_ctx);

/**
 * @brief  获取运行统计
 * @param  hcsi: 驱动句柄指针
 * @param  stats: 输出统计结构体
 * @retval MIPI_CSI2_OK 成功
 */
mipi_csi2_err_t mipi_csi2_get_stats(mipi_csi2_handle_t *hcsi, mipi_csi2_stats_t *stats);

/**
 * @brief  复位 MIPI CSI-2 PHY
 * @param  hcsi: 驱动句柄指针
 * @retval MIPI_CSI2_OK 成功
 */
mipi_csi2_err_t mipi_csi2_phy_reset(mipi_csi2_handle_t *hcsi);

/**
 * @brief  获取驱动状态
 * @param  hcsi: 驱动句柄指针
 * @retval 当前状态
 */
mipi_csi2_state_t mipi_csi2_get_state(mipi_csi2_handle_t *hcsi);

/**
 * @brief  获取错误码描述字符串
 * @param  err: 错误码
 * @retval 描述字符串
 */
const char* mipi_csi2_err_to_string(mipi_csi2_err_t err);

/* =============================================================================
 * 中断处理函数 (需在 ISR 中调用)
 * ==========================================================================*/

/**
 * @brief  DCMIPP 全局中断处理
 * @param  hcsi: 驱动句柄指针
 */
void mipi_csi2_irq_handler(mipi_csi2_handle_t *hcsi);

/**
 * @brief  DMA 传输完成回调 (HAL 弱定义覆盖)
 * @param  hcsi: 驱动句柄指针
 */
void mipi_csi2_dma_tc_callback(mipi_csi2_handle_t *hcsi);

/**
 * @brief  DMA 传输错误回调 (HAL 弱定义覆盖)
 * @param  hcsi: 驱动句柄指针
 */
void mipi_csi2_dma_err_callback(mipi_csi2_handle_t *hcsi);

#ifdef __cplusplus
}
#endif

#endif /* __MIPI_CSI2_H */
