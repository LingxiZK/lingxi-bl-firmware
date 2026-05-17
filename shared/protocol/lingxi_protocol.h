/*******************************************************************************
 * @file    lingxi_protocol.h
 * @brief   灵犀智空 Lingxi BL 感知模组 — STM32↔ESP32通信协议
 * @version 1.0.0
 * @date    2026-05-15
 ******************************************************************************/

#ifndef LINGXI_PROTOCOL_H
#define LINGXI_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 协议常量
 *===========================================================================*/
#define LX_SOF              0xAA55      /* 帧起始标志 */
#define LX_EOF              0x55AA      /* 帧结束标志 */
#define LX_MAX_PAYLOAD_LEN  2048        /* 最大载荷长度 */
#define LX_HEADER_LEN       8           /* 帧头长度 */
#define LX_CRC_LEN          2           /* CRC长度 */
#define LX_FRAME_MIN_LEN    12          /* 最小帧长度 */

/* 设备地址 */
#define LX_ADDR_STM32       0x01
#define LX_ADDR_ESP32       0x02
#define LX_ADDR_BROADCAST   0xFF

/*============================================================================
 * 命令码定义
 *===========================================================================*/
typedef enum {
    /* 状态查询 */
    LX_CMD_GET_STATUS     = 0x10,  /* 获取设备状态 */
    LX_CMD_STATUS_RESP    = 0x11,  /* 状态响应 */
    
    /* AI推理结果 */
    LX_CMD_SEND_RESULT    = 0x20,  /* STM32→ESP32: 发送AI检测结果 */
    LX_CMD_RESULT_ACK     = 0x21,  /* 结果确认 */
    
    /* 传感器数据 */
    LX_CMD_SEND_SENSOR    = 0x30,  /* STM32→ESP32: 发送ToF/UWB数据 */
    LX_CMD_SENSOR_ACK     = 0x31,  /* 传感器确认 */
    
    /* WiFi/BLE配置 */
    LX_CMD_WIFI_CONFIG    = 0x40,  /* ESP32→STM32: WiFi配置 */
    LX_CMD_WIFI_STATUS    = 0x41,  /* WiFi状态 */
    LX_CMD_BLE_DATA       = 0x42,  /* ESP32→STM32: BLE数据 */
    
    /* 图传 */
    LX_CMD_VIDEO_START    = 0x50,  /* 启动图传 */
    LX_CMD_VIDEO_STOP     = 0x51,  /* 停止图传 */
    LX_CMD_VIDEO_FRAME    = 0x52,  /* 视频帧数据 */
    
    /* OTA升级 */
    LX_CMD_OTA_START      = 0x60,  /* OTA开始 */
    LX_CMD_OTA_CHUNK      = 0x61,  /* OTA数据块 */
    LX_CMD_OTA_VERIFY     = 0x62,  /* OTA校验 */
    LX_CMD_OTA_COMMIT     = 0x63,  /* OTA提交 */
    LX_CMD_OTA_ROLLBACK   = 0x64,  /* OTA回滚 */
    
    /* 心跳 */
    LX_CMD_HEARTBEAT      = 0x70,  /* 心跳包 */
    LX_CMD_HEARTBEAT_ACK  = 0x71,  /* 心跳响应 */
    
    /* 控制 */
    LX_CMD_SET_MODE       = 0x80,  /* 设置工作模式 */
    LX_CMD_GET_VERSION    = 0x81,  /* 获取版本 */
    LX_CMD_REBOOT         = 0x82,  /* 重启设备 */
    
    LX_CMD_MAX
} lx_cmd_t;

/* 工作模式 */
typedef enum {
    LX_MODE_IDLE      = 0x00,
    LX_MODE_OBSTACLE  = 0x01,  /* 避障模式 */
    LX_MODE_EDGE      = 0x02,  /* 边缘识别模式 */
    LX_MODE_TRACK     = 0x03,  /* 跟踪模式 */
    LX_MODE_RECORD    = 0x04,  /* 录像模式 */
} lx_mode_t;

/*============================================================================
 * 数据结构
 *===========================================================================*/

/* 帧头 */
typedef struct __attribute__((packed)) {
    uint16_t sof;           /* 起始标志 0xAA55 */
    uint8_t  src_addr;      /* 源地址 */
    uint8_t  dst_addr;      /* 目的地址 */
    uint8_t  cmd;           /* 命令码 */
    uint8_t  flags;         /* 标志位 */
    uint16_t payload_len;   /* 载荷长度 */
} lx_frame_header_t;

/* 完整帧 */
typedef struct __attribute__((packed)) {
    lx_frame_header_t header;
    uint8_t  payload[LX_MAX_PAYLOAD_LEN];
    uint16_t crc;           /* CRC16 */
    uint16_t eof;           /* 结束标志 0x55AA */
} lx_frame_t;

/* AI检测结果 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;     /* 时间戳 ms */
    uint8_t  mode;          /* 当前模式 */
    uint8_t  num_objects;   /* 检测目标数量 */
    struct {
        uint8_t  class_id;      /* 类别: 0=人,1=车,2=墙,3=树,4=障碍 */
        uint16_t x;             /* 左上角X */
        uint16_t y;             /* 左上角Y */
        uint16_t w;             /* 宽度 */
        uint16_t h;             /* 高度 */
        uint8_t  confidence;    /* 置信度 0-100 */
    } objects[10];          /* 最多10个目标 */
} lx_result_t;

/* 传感器数据 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;
    uint16_t tof_distance_mm;   /* ToF距离 mm */
    uint16_t tof_status;        /* ToF状态 */
    int32_t  uwb_x_mm;          /* UWB X坐标 mm */
    int32_t  uwb_y_mm;          /* UWB Y坐标 mm */
    int32_t  uwb_z_mm;          /* UWB Z坐标 mm */
    uint16_t uwb_quality;       /* UWB定位质量 */
} lx_sensor_t;

/* 设备状态 */
typedef struct __attribute__((packed)) {
    uint8_t  device_addr;
    uint8_t  mode;
    uint8_t  status;            /* 0=正常,1=警告,2=错误 */
    uint8_t  wifi_connected;
    uint8_t  ble_connected;
    uint16_t cpu_load;          /* CPU负载 % */
    uint16_t npu_load;          /* NPU负载 % */
    uint32_t free_heap;         /* 空闲内存 */
    uint16_t temperature;       /* 温度 x10°C */
    uint16_t voltage;           /* 电压 mV */
} lx_status_t;

/* WiFi配置 */
typedef struct __attribute__((packed)) {
    uint8_t  enable;
    uint8_t  mode;              /* 0=STA,1=AP */
    char     ssid[32];
    char     password[64];
    uint8_t  channel;
} lx_wifi_config_t;

/* OTA数据块 */
typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint32_t chunk_offset;
    uint16_t chunk_len;
    uint8_t  data[1024];
} lx_ota_chunk_t;

/*============================================================================
 * API函数
 *===========================================================================*/

/* 初始化协议层 */
void lx_protocol_init(void);

/* 打包帧 */
int lx_pack_frame(uint8_t src, uint8_t dst, uint8_t cmd,
                   const uint8_t *payload, uint16_t len,
                   uint8_t *out_buf, uint16_t *out_len);

/* 解包帧 */
int lx_unpack_frame(const uint8_t *buf, uint16_t len, lx_frame_t *frame);

/* 计算CRC16 */
uint16_t lx_crc16(const uint8_t *data, uint16_t len);

/* 发送帧（需由底层驱动实现） */
int lx_send_frame(const uint8_t *buf, uint16_t len);

/* 注册命令处理回调 */
typedef void (*lx_cmd_handler_t)(const lx_frame_t *frame);
void lx_register_handler(uint8_t cmd, lx_cmd_handler_t handler);

/* 处理接收到的字节流 */
void lx_process_byte(uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* LINGXI_PROTOCOL_H */
