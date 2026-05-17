/*******************************************************************************
 * @file    lingxi_protocol.c
 * @brief   灵犀智空通信协议实现
 * @version 1.0.0
 ******************************************************************************/

#include "lingxi_protocol.h"
#include <string.h>

/*============================================================================
 * 私有变量
 *===========================================================================*/
static lx_cmd_handler_t s_handlers[LX_CMD_MAX] = {NULL};
static uint8_t s_rx_buf[LX_FRAME_MIN_LEN + LX_MAX_PAYLOAD_LEN];
static uint16_t s_rx_idx = 0;
static bool s_sof_found = false;

/*============================================================================
 * CRC16实现 (CCITT-FALSE)
 *===========================================================================*/
uint16_t lx_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

/*============================================================================
 * 帧打包
 *===========================================================================*/
int lx_pack_frame(uint8_t src, uint8_t dst, uint8_t cmd,
                   const uint8_t *payload, uint16_t len,
                   uint8_t *out_buf, uint16_t *out_len)
{
    if (len > LX_MAX_PAYLOAD_LEN || out_buf == NULL || out_len == NULL) {
        return -1;
    }

    uint16_t idx = 0;

    /* SOF */
    out_buf[idx++] = (LX_SOF >> 8) & 0xFF;
    out_buf[idx++] = LX_SOF & 0xFF;

    /* Header */
    out_buf[idx++] = src;
    out_buf[idx++] = dst;
    out_buf[idx++] = cmd;
    out_buf[idx++] = 0x00;  /* flags */
    out_buf[idx++] = (len >> 8) & 0xFF;
    out_buf[idx++] = len & 0xFF;

    /* Payload */
    if (len > 0 && payload != NULL) {
        memcpy(&out_buf[idx], payload, len);
        idx += len;
    }

    /* CRC (header + payload) */
    uint16_t crc = lx_crc16(&out_buf[2], 6 + len);
    out_buf[idx++] = (crc >> 8) & 0xFF;
    out_buf[idx++] = crc & 0xFF;

    /* EOF */
    out_buf[idx++] = (LX_EOF >> 8) & 0xFF;
    out_buf[idx++] = LX_EOF & 0xFF;

    *out_len = idx;
    return 0;
}

/*============================================================================
 * 帧解包
 *===========================================================================*/
int lx_unpack_frame(const uint8_t *buf, uint16_t len, lx_frame_t *frame)
{
    if (buf == NULL || frame == NULL || len < LX_FRAME_MIN_LEN) {
        return -1;
    }

    uint16_t idx = 0;

    /* Check SOF */
    uint16_t sof = ((uint16_t)buf[idx] << 8) | buf[idx + 1];
    if (sof != LX_SOF) {
        return -2;
    }
    idx += 2;

    /* Parse header */
    frame->header.sof = sof;
    frame->header.src_addr = buf[idx++];
    frame->header.dst_addr = buf[idx++];
    frame->header.cmd = buf[idx++];
    frame->header.flags = buf[idx++];
    frame->header.payload_len = ((uint16_t)buf[idx] << 8) | buf[idx + 1];
    idx += 2;

    if (frame->header.payload_len > LX_MAX_PAYLOAD_LEN) {
        return -3;
    }

    uint16_t expected_len = LX_HEADER_LEN + frame->header.payload_len + LX_CRC_LEN + 2;
    if (len < expected_len) {
        return -4;
    }

    /* Payload */
    if (frame->header.payload_len > 0) {
        memcpy(frame->payload, &buf[idx], frame->header.payload_len);
        idx += frame->header.payload_len;
    }

    /* CRC */
    uint16_t rx_crc = ((uint16_t)buf[idx] << 8) | buf[idx + 1];
    idx += 2;

    uint16_t calc_crc = lx_crc16(&buf[2], 6 + frame->header.payload_len);
    if (rx_crc != calc_crc) {
        return -5;
    }
    frame->crc = rx_crc;

    /* EOF */
    uint16_t eof = ((uint16_t)buf[idx] << 8) | buf[idx + 1];
    if (eof != LX_EOF) {
        return -6;
    }
    frame->eof = eof;

    return 0;
}

/*============================================================================
 * 协议初始化
 *===========================================================================*/
void lx_protocol_init(void)
{
    memset(s_handlers, 0, sizeof(s_handlers));
    s_rx_idx = 0;
    s_sof_found = false;
}

/*============================================================================
 * 注册命令处理函数
 *===========================================================================*/
void lx_register_handler(uint8_t cmd, lx_cmd_handler_t handler)
{
    if (cmd < LX_CMD_MAX) {
        s_handlers[cmd] = handler;
    }
}

/*============================================================================
 * 字节流处理（状态机）
 *===========================================================================*/
void lx_process_byte(uint8_t byte)
{
    if (!s_sof_found) {
        /* 等待SOF */
        s_rx_buf[s_rx_idx++] = byte;
        if (s_rx_idx >= 2) {
            uint16_t sof = ((uint16_t)s_rx_buf[0] << 8) | s_rx_buf[1];
            if (sof == LX_SOF) {
                s_sof_found = true;
            } else {
                /* 滑动窗口 */
                s_rx_buf[0] = s_rx_buf[1];
                s_rx_idx = 1;
            }
        }
    } else {
        /* 接收帧体 */
        s_rx_buf[s_rx_idx++] = byte;

        if (s_rx_idx >= LX_FRAME_MIN_LEN) {
            uint16_t payload_len = ((uint16_t)s_rx_buf[6] << 8) | s_rx_buf[7];
            uint16_t expected_len = LX_FRAME_MIN_LEN + payload_len;

            if (s_rx_idx >= expected_len) {
                lx_frame_t frame;
                int ret = lx_unpack_frame(s_rx_buf, s_rx_idx, &frame);
                if (ret == 0) {
                    /* 找到完整帧，分发处理 */
                    if (frame.header.cmd < LX_CMD_MAX && s_handlers[frame.header.cmd] != NULL) {
                        s_handlers[frame.header.cmd](&frame);
                    }
                }
                /* 重置接收 */
                s_rx_idx = 0;
                s_sof_found = false;
            }
        }

        /* 防止溢出 */
        if (s_rx_idx >= sizeof(s_rx_buf)) {
            s_rx_idx = 0;
            s_sof_found = false;
        }
    }
}
