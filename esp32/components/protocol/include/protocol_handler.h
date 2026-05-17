#pragma once
/**
 * @file protocol_handler.h
 * @brief 通信协议处理组件
 * @version 3.2
 *
 * 处理与 STM32 的自定义轻量协议
 * - 命令帧解析与响应
 * - 数据帧封装与发送
 * - 心跳/状态帧管理
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lingxi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * 回调类型
 *===========================================================================*/

/**
 * @brief 命令处理回调
 */
typedef esp_err_t (*proto_cmd_handler_t)(const lx_frame_t *cmd, lx_frame_t *resp, void *arg);

/**
 * @brief 事件回调
 */
typedef void (*proto_event_cb_t)(lx_frame_type_t type, const void *payload, void *arg);

/*=============================================================================
 * API 函数
 *===========================================================================*/

/**
 * @brief 初始化协议处理器
 */
esp_err_t proto_handler_init(void);

/**
 * @brief 反初始化
 */
esp_err_t proto_handler_deinit(void);

/**
 * @brief 注册命令处理器
 * @param type    命令类型
 * @param handler 处理函数
 * @param arg     用户参数
 */
esp_err_t proto_handler_register_cmd(lx_frame_type_t type, proto_cmd_handler_t handler, void *arg);

/**
 * @brief 注销命令处理器
 */
esp_err_t proto_handler_unregister_cmd(lx_frame_type_t type);

/**
 * @brief 注册事件回调
 */
esp_err_t proto_handler_register_event(proto_event_cb_t cb, void *arg);

/**
 * @brief 处理接收到的帧
 */
esp_err_t proto_handler_process_frame(const lx_frame_t *frame);

/**
 * @brief 发送状态帧
 */
esp_err_t proto_handler_send_status(const lx_payload_status_t *status);

/**
 * @brief 发送避障数据
 */
esp_err_t proto_handler_send_obstacle(const lx_payload_obstacle_t *obstacle);

/**
 * @brief 发送 ToF 数据
 */
esp_err_t proto_handler_send_tof(const lx_payload_tof_t *tof);

/**
 * @brief 发送 UWB 数据
 */
esp_err_t proto_handler_send_uwb(const lx_payload_uwb_t *uwb);

/**
 * @brief 发送日志
 */
esp_err_t proto_handler_send_log(const char *log, uint16_t len);

/**
 * @brief 发送事件
 */
esp_err_t proto_handler_send_event(lx_frame_type_t event_type, const uint8_t *data, uint16_t len);

/**
 * @brief 发送应答
 */
esp_err_t proto_handler_send_ack(lx_frame_type_t ack_type, const uint8_t *data, uint16_t len);

/**
 * @brief 发送心跳
 */
esp_err_t proto_handler_send_ping(void);

/**
 * @brief 发送心跳响应
 */
esp_err_t proto_handler_send_pong(void);

#ifdef __cplusplus
}
#endif
