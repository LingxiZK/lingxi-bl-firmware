/*******************************************************************************
 * @file    bl_protocol.h
 * @brief   Bootloader UART OTA 协议层
 * @version 1.0.0
 ******************************************************************************/

#ifndef BL_PROTOCOL_H
#define BL_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "bl_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 协议常量
 *===========================================================================*/
#define BL_SOF                  0xAA
#define BL_MAX_PAYLOAD_LEN      1024
#define BL_HEADER_LEN           5       /* SOF + LEN(2) + CMD + SEQ */
#define BL_CRC_LEN              2
#define BL_FRAME_MAX_LEN        (BL_HEADER_LEN + BL_MAX_PAYLOAD_LEN + BL_CRC_LEN)

/* 命令码 — 与 lingxi_protocol.h 中 LX_CMD_OTA_* 保持一致 */
typedef enum {
    BL_CMD_ENTER_BOOT   = 0x01,  /* 进入 Bootloader */
    BL_CMD_GET_STATUS   = 0x10,  /* 获取状态 */
    BL_CMD_STATUS_RESP  = 0x11,  /* 状态响应 */
    BL_CMD_OTA_START    = 0x60,  /* OTA 开始 */
    BL_CMD_OTA_START_ACK= 0x61,  /* OTA 开始确认 */
    BL_CMD_OTA_CHUNK    = 0x62,  /* OTA 数据块 */
    BL_CMD_OTA_CHUNK_ACK= 0x63,  /* OTA 块确认 */
    BL_CMD_OTA_VERIFY   = 0x64,  /* OTA 校验 */
    BL_CMD_OTA_VERIFY_ACK=0x65,  /* OTA 校验结果 */
    BL_CMD_OTA_COMMIT   = 0x66,  /* OTA 提交 */
    BL_CMD_OTA_COMMIT_ACK=0x67,  /* OTA 提交确认 */
    BL_CMD_OTA_ROLLBACK = 0x68,  /* OTA 回滚 */
    BL_CMD_REBOOT       = 0x82,  /* 重启设备 */
} bl_cmd_t;

/* 响应状态 */
typedef enum {
    BL_ACK_OK       = 0x00,
    BL_ACK_ERR_CRC  = 0x01,
    BL_ACK_ERR_SIZE = 0x02,
    BL_ACK_ERR_CMD  = 0x03,
    BL_ACK_ERR_FLASH= 0x04,
    BL_ACK_ERR_BUSY = 0x05,
    BL_ACK_ERR_VERIFY=0x06,
} bl_ack_status_t;

/*============================================================================
 * 数据结构
 *===========================================================================*/

typedef struct __attribute__((packed)) {
    uint8_t  sof;
    uint16_t payload_len;   /* 小端 */
    uint8_t  cmd;
    uint8_t  seq;
    uint8_t  payload[BL_MAX_PAYLOAD_LEN];
    uint16_t crc16;         /* 小端 */
} bl_frame_t;

/* OTA_START 请求 payload */
typedef struct __attribute__((packed)) {
    uint32_t total_size;    /* 固件总大小 */
    uint32_t crc32;         /* 整体 CRC32 */
    uint8_t  version[16];   /* 版本号 */
    uint8_t  target_chip;   /* 0=STM32, 1=ESP32 */
    uint8_t  reserved[3];
} bl_ota_start_req_t;

/* OTA_START_ACK payload */
typedef struct __attribute__((packed)) {
    uint8_t  status;        /* BL_ACK_XXX */
    uint32_t max_chunk;     /* 单块最大长度 (通常1024) */
    uint32_t partition_addr;/* 分配的分区起始地址 */
} bl_ota_start_ack_t;

/* OTA_CHUNK 请求 payload */
typedef struct __attribute__((packed)) {
    uint32_t offset;        /* 偏移量 */
    uint16_t data_len;      /* 本块数据长度 */
    uint8_t  data[BL_MAX_PAYLOAD_LEN - 6];
} bl_ota_chunk_req_t;

/* OTA_CHUNK_ACK payload */
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint32_t next_offset;   /* 下一块期望偏移 */
} bl_ota_chunk_ack_t;

/* OTA_VERIFY_ACK payload */
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint32_t calc_crc32;    /* 实际计算的 CRC32 */
} bl_ota_verify_ack_t;

/* STATUS_RESP payload */
typedef struct __attribute__((packed)) {
    uint8_t  bl_version[3]; /* major, minor, patch */
    uint8_t  active_part;   /* 0=A, 1=B */
    uint8_t  app_a_valid;
    uint8_t  app_b_valid;
    uint8_t  mode;          /* 0=bootloader, 1=app */
    uint32_t uptime_ms;
} bl_status_resp_t;

/*============================================================================
 * API
 *===========================================================================*/

void bl_protocol_init(void);
void bl_protocol_process_byte(uint8_t byte);
void bl_protocol_send_ack(uint8_t cmd, uint8_t seq, uint8_t status,
                          const uint8_t *payload, uint16_t payload_len);

/* 注册命令处理器 */
typedef void (*bl_cmd_handler_t)(const bl_frame_t *frame);
void bl_protocol_register_handler(uint8_t cmd, bl_cmd_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif /* BL_PROTOCOL_H */
