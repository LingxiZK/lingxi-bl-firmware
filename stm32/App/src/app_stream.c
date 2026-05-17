/**
  ******************************************************************************
  * @file    app_stream.c
  * @brief   图像流传输层实现 — 帧分片 + SDIO 发送
  * @author  Lingxi Team
  * @version v4.0
  * @date    2026-05-17
  ******************************************************************************
  * 分片算法:
  *   帧数据 N 字节 → ceil(N / STREAM_FRAG_DATA_SIZE) 个分片
  *   每个分片 = frag_header (12B) + frag_data (≤1516B)
  *   首个分片: frag_header + frame_header (28B) + 剩余 payload
  *   SDIO 包 = sdio_packet_t.header (8B) + frag_data (≤1528B)
  *   合计每 SDIO 包 ≤ 1536 字节 (硬件限制)
  ******************************************************************************
  */

#include "app_stream.h"
#include "bsp_sdio.h"
#include <string.h>

/* =============================================================================
 * 私有数据
 * ==========================================================================*/
typedef struct {
    uint32_t    next_frame_id;
    app_stream_stats_t stats;
} app_stream_ctx_t;

static app_stream_ctx_t s_ctx = {0};

/* =============================================================================
 * 实现
 * ==========================================================================*/
void app_stream_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.next_frame_id = 1;
}

lingxi_err_t app_stream_send_frame(const uint8_t *frame, uint32_t frame_size,
                                    uint16_t width, uint16_t height,
                                    uint8_t format, uint64_t timestamp_us)
{
    if (frame == NULL || frame_size == 0) {
        return LINGXI_ERR_INVALID_PARAM;
    }

    uint32_t frame_id = s_ctx.next_frame_id++;

    /* ── 计算分片数 ── */
    /* 首个分片: frag_header(12) + frame_header(28) = 40 字节开销 */
    /* 后续分片: frag_header(12) 开销 */
    const uint32_t payload_per_frag = STREAM_FRAG_DATA_SIZE;
    const uint32_t first_frag_overhead = sizeof(app_stream_fragment_header_t)
                                       + sizeof(app_stream_frame_header_t);
    const uint32_t first_frag_payload = payload_per_frag - first_frag_overhead
                                         + sizeof(app_stream_fragment_header_t);

    /* 实际首个分片能给图像数据的空间 */
    uint32_t first_data_cap = payload_per_frag - sizeof(app_stream_fragment_header_t)
                               - sizeof(app_stream_frame_header_t);

    uint32_t remaining = frame_size;
    uint32_t frag_count = 0;

    /* 分片 0: 含帧头 */
    {
        frag_count++;
        uint32_t data_in_this = (remaining > first_data_cap) ? first_data_cap : remaining;
        remaining -= data_in_this;
    }

    /* 后续分片 */
    while (remaining > 0) {
        frag_count++;
        uint32_t data_per = payload_per_frag - sizeof(app_stream_fragment_header_t);
        remaining = (remaining > data_per) ? (remaining - data_per) : 0;
    }

    /* 重置 remaining 并开始发送 */
    remaining = frame_size;
    const uint8_t *src_ptr = frame;
    uint16_t frag_idx = 0;

    for (frag_idx = 0; frag_idx < frag_count; frag_idx++) {
        sdio_packet_t sdio_pkt;
        memset(&sdio_pkt, 0, sizeof(sdio_pkt));
        sdio_pkt.magic = 0xA5;
        sdio_pkt.type  = SDIO_PKT_IMAGE_FRAME;
        sdio_pkt.seq   = (uint16_t)(frame_id & 0xFFFF);

        uint8_t *payload = sdio_pkt.data;
        uint32_t payload_len = 0;

        /* 写入分片头 */
        app_stream_fragment_header_t *fh = (app_stream_fragment_header_t *)payload;
        fh->frame_id   = frame_id;
        fh->frag_idx   = frag_idx;
        fh->total_frags = (uint16_t)frag_count;
        fh->flags      = (frag_idx == 0) ? 0x01 : 0x00;  /* bit0: 首个分片 */
        payload_len += sizeof(app_stream_fragment_header_t);

        /* 首个分片: 写入帧头 */
        if (frag_idx == 0) {
            app_stream_frame_header_t *frame_hdr = (app_stream_frame_header_t *)
                (payload + sizeof(app_stream_fragment_header_t));
            frame_hdr->magic        = 0x4C58494D;  /* "LXIM" */
            frame_hdr->frame_id     = frame_id;
            frame_hdr->timestamp_us = timestamp_us;
            frame_hdr->width        = width;
            frame_hdr->height       = height;
            frame_hdr->format       = format;
            frame_hdr->reserved     = 0;
            frame_hdr->frame_size   = frame_size;
            payload_len += sizeof(app_stream_frame_header_t);
        }

        /* 写入图像数据 */
        uint32_t max_data = payload_per_frag - sizeof(app_stream_fragment_header_t)
                            - ((frag_idx == 0) ? sizeof(app_stream_frame_header_t) : 0);
        uint32_t data_len = (remaining > max_data) ? max_data : remaining;

        memcpy(payload + payload_len, src_ptr, data_len);
        payload_len += data_len;
        src_ptr  += data_len;
        remaining -= data_len;

        /* 设置 SDIO 包长度 */
        sdio_pkt.len = payload_len;

        /* 计算 CRC16 (简单 XOR, 实际可用硬件 CRC) */
        uint16_t crc = 0;
        for (uint32_t i = 0; i < payload_len; i++) {
            crc ^= (uint16_t)payload[i];
        }
        sdio_pkt.crc16 = crc;

        /* 发送 */
        lingxi_err_t err = bsp_sdio_send_packet(&sdio_pkt, 100);
        if (err != LINGXI_OK) {
            s_ctx.stats.send_errors++;
            return err;
        }

        s_ctx.stats.fragments_sent++;
    }

    /* ── 更新统计 ── */
    s_ctx.stats.frames_sent++;
    s_ctx.stats.last_frame_size     = frame_size;
    s_ctx.stats.last_fragment_count = frag_count;

    return LINGXI_OK;
}

lingxi_err_t app_stream_get_stats(app_stream_stats_t *stats)
{
    if (stats == NULL) return LINGXI_ERR_NULL_PTR;
    memcpy(stats, &s_ctx.stats, sizeof(app_stream_stats_t));
    return LINGXI_OK;
}
