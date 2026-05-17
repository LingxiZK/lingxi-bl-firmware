/*******************************************************************************
 * @file    bl_protocol.c
 * @brief   Bootloader UART OTA 协议实现 (v1.1.0)
 * @version 1.1.0
 *
 * 修正说明 (v1.1.0):
 *   STM32N657 没有用户 Internal Flash，所有 OTA 接收先缓存到 RAM，
 *   再流式刷入 W25Q512 XIP Flash。避免每个 chunk 直接操作 Flash 导致 UART 超时。
 ******************************************************************************/

#include "bl_protocol.h"
#include "bl_flash.h"
#include "stm32n6xx_hal.h"
#include <string.h>
#include <stdio.h>

/* 外部声明 */
extern UART_HandleTypeDef* bl_get_uart_handle(void);
extern void bl_set_enter_boot(bool val);
extern bool bl_get_enter_boot(void);
extern uint32_t bl_get_tick_ms(void);
extern int uart1_send_string(const char *str);

/* 局部状态 */
static bl_cmd_handler_t s_handlers[256] = {NULL};
static uint8_t s_rx_buf[BL_FRAME_MAX_LEN];
static uint16_t s_rx_idx = 0;
static uint16_t s_expect_len = 0;
static bool s_in_frame = false;

/* OTA 状态 */
static struct {
    bool     active;
    uint32_t total_size;
    uint32_t rx_offset;          /* 已收到 RAM 的最大偏移 */
    uint32_t flash_write_offset; /* 已刷入 XIP Flash 的最大偏移 */
    uint32_t target_addr;        /* XIP Flash 目标地址 */
    uint32_t expected_crc;
    uint8_t  version[16];
    uint32_t last_activity_ms;
} s_ota_ctx;

/* 帧解析状态机 */
typedef enum {
    ST_WAIT_SOF,
    ST_READ_LEN_LO,
    ST_READ_LEN_HI,
    ST_READ_CMD,
    ST_READ_SEQ,
    ST_READ_PAYLOAD,
    ST_READ_CRC_LO,
    ST_READ_CRC_HI,
} rx_state_t;

static rx_state_t s_state = ST_WAIT_SOF;

/*============================================================================
 * 内部函数
 *===========================================================================*/
static uint16_t bl_crc16_modbus(const uint8_t *data, uint16_t len);
static void bl_send_raw(const uint8_t *buf, uint16_t len);
static bl_err_t ota_flush_sector_to_flash(uint32_t sector_offset);
static bl_err_t ota_flush_all_to_flash(void);
static void handle_enter_boot(const bl_frame_t *frame);
static void handle_get_status(const bl_frame_t *frame);
static void handle_ota_start(const bl_frame_t *frame);
static void handle_ota_chunk(const bl_frame_t *frame);
static void handle_ota_verify(const bl_frame_t *frame);
static void handle_ota_commit(const bl_frame_t *frame);
static void handle_ota_rollback(const bl_frame_t *frame);
static void handle_reboot(const bl_frame_t *frame);

/*============================================================================
 * 初始化
 *===========================================================================*/
void bl_protocol_init(void)
{
    memset(s_handlers, 0, sizeof(s_handlers));
    memset(&s_ota_ctx, 0, sizeof(s_ota_ctx));
    s_rx_idx = 0;
    s_expect_len = 0;
    s_in_frame = false;
    s_state = ST_WAIT_SOF;

    /* 注册默认处理器 */
    bl_protocol_register_handler(BL_CMD_ENTER_BOOT,   handle_enter_boot);
    bl_protocol_register_handler(BL_CMD_GET_STATUS,   handle_get_status);
    bl_protocol_register_handler(BL_CMD_OTA_START,    handle_ota_start);
    bl_protocol_register_handler(BL_CMD_OTA_CHUNK,    handle_ota_chunk);
    bl_protocol_register_handler(BL_CMD_OTA_VERIFY,   handle_ota_verify);
    bl_protocol_register_handler(BL_CMD_OTA_COMMIT,   handle_ota_commit);
    bl_protocol_register_handler(BL_CMD_OTA_ROLLBACK, handle_ota_rollback);
    bl_protocol_register_handler(BL_CMD_REBOOT,       handle_reboot);
}

void bl_protocol_register_handler(uint8_t cmd, bl_cmd_handler_t handler)
{
    if (cmd < 256) {
        s_handlers[cmd] = handler;
    }
}

/*============================================================================
 * 字节流处理 (中断回调调用)
 *===========================================================================*/
void bl_protocol_process_byte(uint8_t byte)
{
    switch (s_state) {
    case ST_WAIT_SOF:
        if (byte == BL_SOF) {
            s_rx_buf[0] = byte;
            s_rx_idx = 1;
            s_state = ST_READ_LEN_LO;
        }
        break;

    case ST_READ_LEN_LO:
        s_rx_buf[s_rx_idx++] = byte;
        s_state = ST_READ_LEN_HI;
        break;

    case ST_READ_LEN_HI:
        s_rx_buf[s_rx_idx++] = byte;
        s_expect_len = s_rx_buf[1] | (s_rx_buf[2] << 8);
        if (s_expect_len > BL_MAX_PAYLOAD_LEN) {
            s_state = ST_WAIT_SOF;  /* 长度异常，丢弃 */
            break;
        }
        s_state = ST_READ_CMD;
        break;

    case ST_READ_CMD:
        s_rx_buf[s_rx_idx++] = byte;
        s_state = ST_READ_SEQ;
        break;

    case ST_READ_SEQ:
        s_rx_buf[s_rx_idx++] = byte;
        if (s_expect_len == 0) {
            s_state = ST_READ_CRC_LO;
        } else {
            s_state = ST_READ_PAYLOAD;
        }
        break;

    case ST_READ_PAYLOAD:
        s_rx_buf[s_rx_idx++] = byte;
        if (s_rx_idx >= (BL_HEADER_LEN + s_expect_len)) {
            s_state = ST_READ_CRC_LO;
        }
        break;

    case ST_READ_CRC_LO:
        s_rx_buf[s_rx_idx++] = byte;
        s_state = ST_READ_CRC_HI;
        break;

    case ST_READ_CRC_HI:
        s_rx_buf[s_rx_idx++] = byte;
        {
            /* 完整帧已收到 */
            uint16_t calc_crc = bl_crc16_modbus(s_rx_buf, s_rx_idx - 2);
            uint16_t recv_crc = s_rx_buf[s_rx_idx - 2] | (s_rx_buf[s_rx_idx - 1] << 8);

            if (calc_crc == recv_crc) {
                bl_frame_t frame;
                frame.sof = s_rx_buf[0];
                frame.payload_len = s_rx_buf[1] | (s_rx_buf[2] << 8);
                frame.cmd = s_rx_buf[3];
                frame.seq = s_rx_buf[4];
                memcpy(frame.payload, &s_rx_buf[5], frame.payload_len);
                frame.crc16 = recv_crc;

                if (s_handlers[frame.cmd] != NULL) {
                    s_handlers[frame.cmd](&frame);
                } else {
                    bl_protocol_send_ack(frame.cmd, frame.seq, BL_ACK_ERR_CMD, NULL, 0);
                }
            } else {
                /* CRC 错误，发送 NACK */
                bl_protocol_send_ack(s_rx_buf[3], s_rx_buf[4], BL_ACK_ERR_CRC, NULL, 0);
            }
        }
        s_state = ST_WAIT_SOF;
        s_rx_idx = 0;
        break;
    }
}

/*============================================================================
 * 发送 ACK / 响应
 *===========================================================================*/
void bl_protocol_send_ack(uint8_t cmd, uint8_t seq, uint8_t status,
                          const uint8_t *payload, uint16_t payload_len)
{
    uint8_t buf[BL_FRAME_MAX_LEN];
    uint16_t idx = 0;

    buf[idx++] = BL_SOF;
    buf[idx++] = (payload_len + 1) & 0xFF;       /* payload = status(1) + data */
    buf[idx++] = ((payload_len + 1) >> 8) & 0xFF;
    buf[idx++] = cmd;                            /* 响应命令与请求相同 */
    buf[idx++] = seq;
    buf[idx++] = status;
    if (payload_len > 0 && payload != NULL) {
        memcpy(&buf[idx], payload, payload_len);
        idx += payload_len;
    }

    uint16_t crc = bl_crc16_modbus(buf, idx);
    buf[idx++] = crc & 0xFF;
    buf[idx++] = (crc >> 8) & 0xFF;

    bl_send_raw(buf, idx);
}

static void bl_send_raw(const uint8_t *buf, uint16_t len)
{
    UART_HandleTypeDef *huart = bl_get_uart_handle();
    if (huart != NULL) {
        HAL_UART_Transmit(huart, (uint8_t*)buf, len, 100);
    }
}

/*============================================================================
 * CRC16 (Modbus)
 *===========================================================================*/
static uint16_t bl_crc16_modbus(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/*============================================================================
 * 流式 Flash 刷入 (从 RAM 缓存刷入 XIP Flash)
 *===========================================================================*/

/**
 * @brief 将一个 4KB sector 从 RAM 缓存刷入 XIP Flash
 * @param sector_offset sector 在目标分区中的偏移(必须 4KB 对齐)
 * @retval BL_OK 或错误码
 */
static bl_err_t ota_flush_sector_to_flash(uint32_t sector_offset)
{
    uint32_t flash_addr = s_ota_ctx.target_addr + sector_offset;
    uint8_t *ram_src = (uint8_t*)(BL_OTA_RAM_CACHE_ADDR + sector_offset);

    /* 擦除对应 XIP Flash sector */
    bl_err_t err = bl_xspi_erase_sector(flash_addr);
    if (err != BL_OK) {
        return err;
    }

    /* 分页写入 (4KB / 256B = 16 页) */
    for (uint32_t page = 0; page < (W25Q_SECTOR_SIZE / W25Q_PAGE_SIZE); page++) {
        uint32_t page_offset = page * W25Q_PAGE_SIZE;
        err = bl_xspi_write_page(flash_addr + page_offset,
                                  ram_src + page_offset, W25Q_PAGE_SIZE);
        if (err != BL_OK) {
            return err;
        }
    }

    return BL_OK;
}

/**
 * @brief 将所有已接收的 RAM 缓存数据刷入 XIP Flash
 * @retval BL_OK 或错误码
 */
static bl_err_t ota_flush_all_to_flash(void)
{
    if (!s_ota_ctx.active || s_ota_ctx.rx_offset == 0) {
        return BL_OK;
    }

    /* 计算需要刷入的最后一个偏移 */
    uint32_t end_offset = s_ota_ctx.rx_offset;

    /* 提交完整的 sectors */
    while ((s_ota_ctx.flash_write_offset + W25Q_SECTOR_SIZE) <= end_offset) {
        bl_err_t err = ota_flush_sector_to_flash(s_ota_ctx.flash_write_offset);
        if (err != BL_OK) {
            return err;
        }
        s_ota_ctx.flash_write_offset += W25Q_SECTOR_SIZE;
    }

    /* 如果还有不满一个 sector 的尾部数据，也要刷入 */
    if (s_ota_ctx.flash_write_offset < end_offset) {
        uint32_t remain = end_offset - s_ota_ctx.flash_write_offset;
        uint32_t flash_addr = s_ota_ctx.target_addr + s_ota_ctx.flash_write_offset;
        uint8_t *ram_src = (uint8_t*)(BL_OTA_RAM_CACHE_ADDR + s_ota_ctx.flash_write_offset);

        /* 擦除最后一个 sector (即使不满) */
        bl_err_t err = bl_xspi_erase_sector(flash_addr);
        if (err != BL_OK) {
            return err;
        }

        /* 分页写入剩余数据 */
        uint32_t written = 0;
        while (written < remain) {
            uint16_t chunk = (remain - written) > W25Q_PAGE_SIZE ? W25Q_PAGE_SIZE : (uint16_t)(remain - written);
            err = bl_xspi_write_page(flash_addr + written, ram_src + written, chunk);
            if (err != BL_OK) {
                return err;
            }
            written += chunk;
        }

        s_ota_ctx.flash_write_offset += remain;
    }

    return BL_OK;
}

/*============================================================================
 * 命令处理器
 *===========================================================================*/
static void handle_enter_boot(const bl_frame_t *frame)
{
    bl_set_enter_boot(true);
    bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_OK, NULL, 0);
}

static void handle_get_status(const bl_frame_t *frame)
{
    bl_status_resp_t resp = {0};
    resp.bl_version[0] = BL_VERSION_MAJOR;
    resp.bl_version[1] = BL_VERSION_MINOR;
    resp.bl_version[2] = BL_VERSION_PATCH;
    resp.mode = 0;  /* bootloader */
    resp.uptime_ms = bl_get_tick_ms();

    /* 读取 info sector */
    bl_info_sector_t info;
    if (bl_info_read(&info) == BL_OK && bl_info_validate(&info)) {
        resp.active_part = info.active_partition;
        resp.app_a_valid = (bl_verify_firmware(BL_APP_A_ADDR,
                              info.app_a.size, info.app_a.crc32) == BL_OK);
        resp.app_b_valid = (bl_verify_firmware(BL_APP_B_ADDR,
                              info.app_b.size, info.app_b.crc32) == BL_OK);
    }

    bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_OK,
                         (uint8_t*)&resp, sizeof(resp));
}

static void handle_ota_start(const bl_frame_t *frame)
{
    if (frame->payload_len < sizeof(bl_ota_start_req_t)) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_SIZE, NULL, 0);
        return;
    }

    const bl_ota_start_req_t *req = (const bl_ota_start_req_t*)frame->payload;

    /* 确定目标分区 (升级到非活动分区) */
    bl_info_sector_t info;
    bl_info_read(&info);
    uint32_t target_addr = (info.active_partition == 0) ? BL_APP_B_ADDR : BL_APP_A_ADDR;

    /* 检查大小 */
    if (req->total_size > BL_APP_A_CODE_SIZE) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_SIZE, NULL, 0);
        return;
    }

    /* 检查 RAM 缓存空间 */
    if (req->total_size > BL_OTA_RAM_CACHE_SIZE) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_SIZE, NULL, 0);
        return;
    }

    /* 清零 RAM 缓存区 */
    memset((void*)BL_OTA_RAM_CACHE_ADDR, 0xFF, BL_OTA_RAM_CACHE_SIZE);

    /* 初始化 OTA 上下文 */
    memset(&s_ota_ctx, 0, sizeof(s_ota_ctx));
    s_ota_ctx.active = true;
    s_ota_ctx.total_size = req->total_size;
    s_ota_ctx.rx_offset = 0;
    s_ota_ctx.flash_write_offset = 0;
    s_ota_ctx.target_addr = target_addr;
    s_ota_ctx.expected_crc = req->crc32;
    memcpy(s_ota_ctx.version, req->version, 16);
    s_ota_ctx.last_activity_ms = bl_get_tick_ms();

    /* 回复 */
    bl_ota_start_ack_t ack = {0};
    ack.status = BL_ACK_OK;
    ack.max_chunk = 1024;
    ack.partition_addr = target_addr;
    bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_OK,
                         (uint8_t*)&ack, sizeof(ack));
}

static void handle_ota_chunk(const bl_frame_t *frame)
{
    if (!s_ota_ctx.active) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_BUSY, NULL, 0);
        return;
    }

    if (frame->payload_len < 6) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_SIZE, NULL, 0);
        return;
    }

    uint32_t offset = frame->payload[0] | (frame->payload[1] << 8) |
                      (frame->payload[2] << 16) | (frame->payload[3] << 24);
    uint16_t data_len = frame->payload[4] | (frame->payload[5] << 8);

    if (data_len > BL_MAX_PAYLOAD_LEN - 6 || (offset + data_len) > s_ota_ctx.total_size) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_SIZE, NULL, 0);
        return;
    }

    /* 检查偏移是否连续 (允许重传同一块，不允许跳跃) */
    if (offset != s_ota_ctx.rx_offset && offset + data_len <= s_ota_ctx.rx_offset) {
        /* 重传已接收过的数据，直接确认 */
        bl_ota_chunk_ack_t ack = {0};
        ack.status = BL_ACK_OK;
        ack.next_offset = s_ota_ctx.rx_offset;
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_OK,
                             (uint8_t*)&ack, sizeof(ack));
        return;
    }

    if (offset != s_ota_ctx.rx_offset) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_SIZE, NULL, 0);
        return;
    }

    /* 写入 RAM 缓存 (极快，几微秒) */
    uint8_t *ram_dst = (uint8_t*)(BL_OTA_RAM_CACHE_ADDR + offset);
    memcpy(ram_dst, &frame->payload[6], data_len);

    s_ota_ctx.rx_offset = offset + data_len;
    s_ota_ctx.last_activity_ms = bl_get_tick_ms();

    /* 流式刷入：如果已接收完整的 4KB sector(s)，同步刷入 XIP Flash */
    while ((s_ota_ctx.flash_write_offset + W25Q_SECTOR_SIZE) <= s_ota_ctx.rx_offset) {
        bl_err_t err = ota_flush_sector_to_flash(s_ota_ctx.flash_write_offset);
        if (err != BL_OK) {
            bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_FLASH, NULL, 0);
            return;
        }
        s_ota_ctx.flash_write_offset += W25Q_SECTOR_SIZE;
    }

    bl_ota_chunk_ack_t ack = {0};
    ack.status = BL_ACK_OK;
    ack.next_offset = s_ota_ctx.rx_offset;
    bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_OK,
                         (uint8_t*)&ack, sizeof(ack));
}

static void handle_ota_verify(const bl_frame_t *frame)
{
    if (!s_ota_ctx.active) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_BUSY, NULL, 0);
        return;
    }

    /* 检查是否收完 */
    if (s_ota_ctx.rx_offset < s_ota_ctx.total_size) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_SIZE, NULL, 0);
        return;
    }

    /* 把剩余的 RAM 缓存刷入 XIP Flash */
    bl_err_t err = ota_flush_all_to_flash();
    if (err != BL_OK) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_FLASH, NULL, 0);
        return;
    }

    /* 从 XIP Flash 读取并校验 CRC */
    err = bl_verify_firmware(s_ota_ctx.target_addr,
                              s_ota_ctx.total_size,
                              s_ota_ctx.expected_crc);

    bl_ota_verify_ack_t ack = {0};
    if (err == BL_OK) {
        ack.status = BL_ACK_OK;
        ack.calc_crc32 = s_ota_ctx.expected_crc;
    } else {
        ack.status = BL_ACK_ERR_VERIFY;
        ack.calc_crc32 = 0; /* 校验失败时不填写 CRC */
    }

    bl_protocol_send_ack(frame->cmd, frame->seq, ack.status,
                         (uint8_t*)&ack, sizeof(ack));
}

static void handle_ota_commit(const bl_frame_t *frame)
{
    if (!s_ota_ctx.active) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_BUSY, NULL, 0);
        return;
    }

    /* 确保所有数据已刷入 Flash */
    bl_err_t err = ota_flush_all_to_flash();
    if (err != BL_OK) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_FLASH, NULL, 0);
        return;
    }

    /* 再次校验 */
    if (bl_verify_firmware(s_ota_ctx.target_addr,
                           s_ota_ctx.total_size,
                           s_ota_ctx.expected_crc) != BL_OK) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_VERIFY, NULL, 0);
        return;
    }

    /* 更新 Info Sector */
    bl_info_sector_t info;
    bl_info_read(&info);

    uint8_t new_part = (info.active_partition == 0) ? 1 : 0;
    if (new_part == 0) {
        info.app_a.size = s_ota_ctx.total_size;
        info.app_a.crc32 = s_ota_ctx.expected_crc;
        memcpy(info.app_a.version, s_ota_ctx.version, 16);
        info.app_a.timestamp = bl_get_tick_ms();
    } else {
        info.app_b.size = s_ota_ctx.total_size;
        info.app_b.crc32 = s_ota_ctx.expected_crc;
        memcpy(info.app_b.version, s_ota_ctx.version, 16);
        info.app_b.timestamp = bl_get_tick_ms();
    }
    info.active_partition = new_part;
    info.upgrade_pending = 0;
    info.boot_count = 0;

    err = bl_info_write(&info);
    if (err != BL_OK) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_FLASH, NULL, 0);
        return;
    }

    s_ota_ctx.active = false;
    bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_OK, NULL, 0);

    /* 延迟 200ms 确保 ACK 发送完毕，然后复位 */
    HAL_Delay(200);
    NVIC_SystemReset();
}

static void handle_ota_rollback(const bl_frame_t *frame)
{
    bl_info_sector_t info;
    bl_err_t err = bl_info_read(&info);
    if (err != BL_OK) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_FLASH, NULL, 0);
        return;
    }

    /* 切换分区 */
    info.active_partition = (info.active_partition == 0) ? 1 : 0;
    info.rollback_request = 0;

    err = bl_info_write(&info);
    if (err != BL_OK) {
        bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_ERR_FLASH, NULL, 0);
        return;
    }

    bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_OK, NULL, 0);
    HAL_Delay(200);
    NVIC_SystemReset();
}

static void handle_reboot(const bl_frame_t *frame)
{
    bl_protocol_send_ack(frame->cmd, frame->seq, BL_ACK_OK, NULL, 0);
    HAL_Delay(200);
    NVIC_SystemReset();
}

/*============================================================================
 * 注册 OTA 处理器 (被 bl_main.c 调用)
 *===========================================================================*/
void bl_ota_register_handlers(void)
{
    /* 已在 bl_protocol_init 中注册 */
}
