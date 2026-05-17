#pragma once
/**
 * @file main.h
 * @brief 主程序头文件
 * @version 3.2
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 任务优先级 */
#define TASK_PRIO_SDIO_RX       10
#define TASK_PRIO_SDIO_TX        10
#define TASK_PRIO_WIFI           8
#define TASK_PRIO_BLE            6
#define TASK_PRIO_PROTO          8
#define TASK_PRIO_OTA            5
#define TASK_PRIO_HEARTBEAT      4
#define TASK_PRIO_MONITOR        3

/* 任务栈大小 */
#define STACK_SIZE_SDIO_RX      4096
#define STACK_SIZE_SDIO_TX      4096
#define STACK_SIZE_WIFI         8192
#define STACK_SIZE_BLE          8192
#define STACK_SIZE_PROTO        4096
#define STACK_SIZE_OTA          8192
#define STACK_SIZE_HEARTBEAT    2048
#define STACK_SIZE_MONITOR      4096

/* 心跳间隔 (ms) */
#define HEARTBEAT_INTERVAL_MS   1000

/* 看门狗超时 (ms) */
#define WDT_TIMEOUT_MS          5000

/**
 * @brief 系统初始化
 */
esp_err_t system_init(void);

/**
 * @brief 系统反初始化
 */
esp_err_t system_deinit(void);

/**
 * @brief 启动所有任务
 */
esp_err_t system_start_tasks(void);

/**
 * @brief 停止所有任务
 */
esp_err_t system_stop_tasks(void);

/**
 * @brief 获取系统状态
 */
typedef struct {
    bool sdio_ready;
    bool wifi_ready;
    bool ble_ready;
    bool proto_ready;
    bool ota_ready;
    uint32_t uptime_ms;
    uint32_t free_heap;
    uint8_t cpu_usage;
} system_status_t;

esp_err_t system_get_status(system_status_t *status);

#ifdef __cplusplus
}
#endif
