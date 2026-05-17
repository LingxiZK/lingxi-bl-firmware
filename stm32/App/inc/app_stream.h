/**
  ******************************************************************************
  * @file    app_stream.h
  * @brief   图像流传输层 — 帧分片/重组 + SDIO 发送
  * @author  Lingxi Team
  * @version v4.0
  * @date    2026-05-17
  ******************************************************************************
  * 协议:
  *   一帧图像 = N 个 SDIO 分片包 (SDIO_PKT_IMAGE_FRAME)
  *   每个分片包携带帧ID、分片索引、总分片数
  *   首个分片还携带帧头 (timestamp, width, height, format)
  *   接收方根据 frame_id + frag_idx 重组
  ******************************************************************************
  */

#ifndef __APP_STREAM_H
#define __APP_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"
#include <stdint.h>

/* =============================================================================
 * 帧头格式 (首个分片的数据负载头部)
 * ==========================================================================*/
typedef struct __attribute__((packed)) {
    uint32_t    magic;          /* 0x4C58494D = "LXIM" */
    uint32_t    frame_id;       /* 帧序列号，单调递增 */
    uint64_t    timestamp_us;   /* 捕获时间戳 (μs since boot) */
    uint16_t    width;          /* 图像宽度 (像素) */
    uint16_t    height;         /* 图像高度 */
    uint8_t     format;         /* 0=RAW8, 1=JPEG */
    uint8_t     reserved;       /* 对齐保留 */
    uint32_t    frame_size;     /* 帧数据总大小 (bytes) */
} app_stream_frame_header_t;

/* =============================================================================
 * 分片元数据 (每个 SDIO 分片包数据负载的前缀)
 * ==========================================================================*/
typedef struct __attribute__((packed)) {
    uint32_t    frame_id;       /* 所属帧 ID */
    uint16_t    frag_idx;       /* 当前分片索引 (0-based) */
    uint16_t    total_frags;    /* 总分片数 */
    uint16_t    frag_size;      /* 本分片有效数据长度 */
    uint8_t     flags;          /* 标志位: bit0=1 表示首个分片(含帧头) */
    uint8_t     reserved;
} app_stream_fragment_header_t;

/* =============================================================================
 * 分片数据负载大小
 *   SDIO 最大包 1536 字节
 *   - sdio_packet_t.header (magic+type+seq+len+crc16) = 8 字节
 *   - sdio_packet_t.data 剩余 = 1528 字节
 *   - app_stream_fragment_header_t = 12 字节
 *   - 有效图像数据 = 1516 字节
 * ==========================================================================*/
#define STREAM_FRAG_DATA_SIZE   (SDIO_MAX_PACKET_SIZE - 12 - 8)
/* 实际: 1528 - 12 = 1516 字节/分片 */

/* 帧头大小 */
#define STREAM_FRAME_HEADER_SIZE sizeof(app_stream_frame_header_t)

/* =============================================================================
 * API
 * ==========================================================================*/

/**
 * @brief  初始化图像流传输层
 */
void app_stream_init(void);

/**
 * @brief  发送一帧图像 (自动分片)
 * @param  frame:       帧数据指针
 * @param  frame_size:  帧数据大小
 * @param  width:       图像宽度
 * @param  height:      图像高度
 * @param  format:      格式 (0=RAW8)
 * @param  timestamp_us:捕获时间戳
 * @retval LINGXI_OK 发送完成, 其他错误码
 * @note   阻塞式发送, 直到所有分片发送完毕
 */
lingxi_err_t app_stream_send_frame(const uint8_t *frame, uint32_t frame_size,
                                    uint16_t width, uint16_t height,
                                    uint8_t format, uint64_t timestamp_us);

/**
 * @brief  获取流传输统计
 */
typedef struct {
    uint32_t frames_sent;
    uint32_t fragments_sent;
    uint32_t frames_dropped;    /* 因发送超时丢弃 */
    uint32_t send_errors;
    uint32_t last_frame_size;
    uint32_t last_fragment_count;
} app_stream_stats_t;

lingxi_err_t app_stream_get_stats(app_stream_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* __APP_STREAM_H */
