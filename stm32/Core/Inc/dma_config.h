/**
  ******************************************************************************
  * @file    dma_config.h
  * @brief   DMA/MDMA HAL 配置头文件 (P2批次)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - MDMA_Channel0: SDMMC1 数据传输
  * - MDMA_Channel1: MIPI CSI-2 帧缓冲
  * - MDMA_Channel2: XSPI1 Flash 读写
  * - 支持循环模式、突发传输、双缓冲
  ******************************************************************************
  */

#ifndef __DMA_CONFIG_H
#define __DMA_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * 错误码定义
 * ==========================================================================*/
typedef enum {
    DMA_OK              = 0,    /* 成功 */
    DMA_ERR_INIT        = -1,   /* 初始化失败 */
    DMA_ERR_PARAM       = -2,   /* 参数错误 */
    DMA_ERR_BUSY        = -3,   /* 通道忙 */
    DMA_ERR_TIMEOUT     = -4,   /* 超时 */
    DMA_ERR_TRANSFER    = -5,   /* 传输错误 */
    DMA_ERR_NOT_INIT    = -6,   /* 未初始化 */
} dma_err_t;

/* =============================================================================
 * MDMA 通道分配
 * ==========================================================================*/
typedef enum {
    MDMA_CH_SDMMC1  = 0,    /* MDMA_Channel0: SDMMC1 数据传输 */
    MDMA_CH_MIPI    = 1,    /* MDMA_Channel1: MIPI CSI-2 帧缓冲 */
    MDMA_CH_XSPI1   = 2,    /* MDMA_Channel2: XSPI1 Flash 读写 */
    MDMA_CH_MAX     = 3,
} mdma_channel_id_t;

/* =============================================================================
 * 传输配置结构体
 * ==========================================================================*/
typedef enum {
    DMA_DIR_P2M = 0,    /* 外设到内存 */
    DMA_DIR_M2P = 1,    /* 内存到外设 */
    DMA_DIR_M2M = 2,    /* 内存到内存 */
} dma_direction_t;

typedef enum {
    DMA_BURST_SINGLE = 0,   /* 单次传输 */
    DMA_BURST_INCR4  = 1,   /* 4 拍突发 */
    DMA_BURST_INCR8  = 2,   /* 8 拍突发 */
    DMA_BURST_INCR16 = 3,   /* 16 拍突发 */
} dma_burst_t;

typedef struct {
    dma_direction_t direction;      /* 传输方向 */
    dma_burst_t     src_burst;      /* 源突发大小 */
    dma_burst_t     dst_burst;      /* 目标突发大小 */
    uint8_t         src_inc;        /* 源地址递增: 0=固定, 1=递增 */
    uint8_t         dst_inc;        /* 目标地址递增 */
    uint8_t         src_width;      /* 源数据宽度: 1/2/4 bytes */
    uint8_t         dst_width;      /* 目标数据宽度 */
    uint8_t         circular;       /* 循环模式: 0=正常, 1=循环 */
    uint8_t         double_buf;     /* 双缓冲模式 */
    uint32_t        priority;       /* 优先级 0-15 */
} dma_xfer_cfg_t;

/* =============================================================================
 * 回调函数类型
 * ==========================================================================*/
typedef void (*dma_tc_callback_t)(mdma_channel_id_t ch, void *user_data);   /* 传输完成回调 */
typedef void (*dma_ht_callback_t)(mdma_channel_id_t ch, void *user_data);   /* 半传输回调 */
typedef void (*dma_err_callback_t)(mdma_channel_id_t ch, void *user_data);  /* 错误回调 */

/* =============================================================================
 * 通道状态结构体
 * ==========================================================================*/
typedef struct {
    MDMA_HandleTypeDef  *hmdma;         /* HAL 句柄 */
    volatile uint8_t     initialized;   /* 初始化标志 */
    volatile uint8_t     busy;          /* 忙标志 */
    uint32_t             xfer_count;    /* 传输计数 */
    uint32_t             err_count;     /* 错误计数 */
    dma_tc_callback_t    tc_cb;         /* 传输完成回调 */
    dma_ht_callback_t    ht_cb;         /* 半传输回调 */
    dma_err_callback_t   err_cb;        /* 错误回调 */
    void                *user_data;     /* 用户数据 */
} mdma_channel_state_t;

/* =============================================================================
 * 配置常量
 * ==========================================================================*/

/* MDMA 通道硬件实例 */
#define MDMA_CH0_INSTANCE       MDMA_Channel0
#define MDMA_CH1_INSTANCE       MDMA_Channel1
#define MDMA_CH2_INSTANCE       MDMA_Channel2

/* 请求映射 */
#define MDMA_REQ_SDMMC1         MDMA_REQUEST_SDMMC1
#define MDMA_REQ_DCMIPP         MDMA_REQUEST_DCMIPP
#define MDMA_REQ_XSPI1          MDMA_REQUEST_XSPI1

/* 缓冲区对齐 */
#define DMA_BUFFER_ALIGN        32      /* Cache line 对齐 */
#define DMA_BUFFER_ALIGN_MASK   (DMA_BUFFER_ALIGN - 1)

/* 默认优先级 */
#define MDMA_PRIO_SDMMC1        12      /* 高优先级: SDIO 通信 */
#define MDMA_PRIO_MIPI          14      /* 最高优先级: 视频数据 */
#define MDMA_PRIO_XSPI1         10      /* 中高优先级: Flash 访问 */

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化所有 MDMA 通道
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_Init(void);

/**
 * @brief  反初始化所有 MDMA 通道
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_DeInit(void);

/**
 * @brief  配置指定 MDMA 通道
 * @param  ch_id: 通道 ID
 * @param  cfg: 传输配置
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_ConfigChannel(mdma_channel_id_t ch_id, const dma_xfer_cfg_t *cfg);

/**
 * @brief  启动单次 MDMA 传输
 * @param  ch_id: 通道 ID
 * @param  src_addr: 源地址
 * @param  dst_addr: 目标地址
 * @param  len: 传输长度 (bytes)
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_StartXfer(mdma_channel_id_t ch_id, uint32_t src_addr, uint32_t dst_addr, uint32_t len);

/**
 * @brief  启动循环 MDMA 传输
 * @param  ch_id: 通道 ID
 * @param  src_addr: 源地址
 * @param  dst_addr: 目标地址
 * @param  len: 传输长度 (bytes)
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_StartCircular(mdma_channel_id_t ch_id, uint32_t src_addr, uint32_t dst_addr, uint32_t len);

/**
 * @brief  停止 MDMA 传输
 * @param  ch_id: 通道 ID
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_Stop(mdma_channel_id_t ch_id);

/**
 * @brief  查询通道是否忙
 * @param  ch_id: 通道 ID
 * @retval true 忙, false 空闲
 */
bool MDMA_IsBusy(mdma_channel_id_t ch_id);

/**
 * @brief  注册回调函数
 * @param  ch_id: 通道 ID
 * @param  tc_cb: 传输完成回调 (可为 NULL)
 * @param  ht_cb: 半传输回调 (可为 NULL)
 * @param  err_cb: 错误回调 (可为 NULL)
 * @param  user_data: 用户数据
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_RegisterCallback(mdma_channel_id_t ch_id,
                                dma_tc_callback_t tc_cb,
                                dma_ht_callback_t ht_cb,
                                dma_err_callback_t err_cb,
                                void *user_data);

/**
 * @brief  获取通道统计信息
 * @param  ch_id: 通道 ID
 * @param  xfer_count: 传输计数输出
 * @param  err_count: 错误计数输出
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_GetStats(mdma_channel_id_t ch_id, uint32_t *xfer_count, uint32_t *err_count);

/**
 * @brief  清除传输完成标志并准备下一次传输
 * @param  ch_id: 通道 ID
 * @retval DMA_OK 成功
 */
dma_err_t MDMA_ClearFlags(mdma_channel_id_t ch_id);

/**
 * @brief  获取指定通道的 HAL 句柄 (供其他驱动链接使用)
 * @param  ch_id: 通道 ID
 * @retval MDMA_HandleTypeDef 指针, NULL 表示未初始化
 */
MDMA_HandleTypeDef* MDMA_GetHandle(mdma_channel_id_t ch_id);

/* =============================================================================
 * 中断服务程序声明 (在 stm32n6xx_it.c 中调用)
 * ==========================================================================*/

/**
 * @brief  MDMA 全局中断处理
 */
void MDMA_IRQHandler(void);

/**
 * @brief  特定通道中断处理 (由 HAL 回调调用)
 * @param  hmdma: MDMA 句柄
 */
void MDMA_ChannelCallback(MDMA_HandleTypeDef *hmdma);

#ifdef __cplusplus
}
#endif

#endif /* __DMA_CONFIG_H */
