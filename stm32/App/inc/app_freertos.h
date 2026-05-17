/**
  ******************************************************************************
  * @file    app_freertos.h
  * @brief   FreeRTOS 任务模型与配置头文件
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 任务优先级: 数值越大优先级越高 (FreeRTOS configMAX_PRIORITIES)
  * - 堆栈大小根据实际调用深度调整
  ******************************************************************************
  */

#ifndef __APP_FREERTOS_H
#define __APP_FREERTOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"
#include "FreeRTOS.h"
#include "event_groups.h"

/* =============================================================================
 * 任务定义
 * ==========================================================================*/

/* 任务优先级 (0-15, 15 最高) */
#define TASK_PRIO_CAMERA        14      /* 摄像头采集: 最高实时性 */
#define TASK_PRIO_INFERENCE     13      /* NPU 推理: 高优先级 */
#define TASK_PRIO_COMM          10      /* 通信: 中高优先级 */
#define TASK_PRIO_SENSOR        8       /* 传感器: 中优先级 */
#define TASK_PRIO_CONTROL       7       /* 主控逻辑: 中优先级 */
#define TASK_PRIO_LOGGER        3       /* 日志: 低优先级 */
#define TASK_PRIO_DIAG          2       /* 诊断: 低优先级 */

/* 任务堆栈大小 (words) */
#define TASK_STACK_CAMERA       1024
#define TASK_STACK_INFERENCE    2048    /* NPU 需要较大堆栈 */
#define TASK_STACK_COMM         1024
#define TASK_STACK_SENSOR       512
#define TASK_STACK_CONTROL      1024
#define TASK_STACK_LOGGER       512
#define TASK_STACK_DIAG         512

/* 任务周期 (ms) */
#define TASK_PERIOD_CAMERA      16      /* 60fps = 16.6ms */
#define TASK_PERIOD_INFERENCE   33      /* 30fps 推理 */
#define TASK_PERIOD_COMM        10      /* 100Hz 通信 */
#define TASK_PERIOD_SENSOR      33      /* 30Hz 传感器 */
#define TASK_PERIOD_CONTROL     20      /* 50Hz 控制 */
#define TASK_PERIOD_LOGGER      1000    /* 1Hz 日志 */
#define TASK_PERIOD_DIAG        5000    /* 0.2Hz 诊断 */

/* =============================================================================
 * 队列与信号量定义
 * ==========================================================================*/

/* 队列深度 */
#define QUEUE_DEPTH_CAM_FRAMES      3
#define QUEUE_DEPTH_INFER_RESULTS     3
#define QUEUE_DEPTH_SENSOR_DATA       5
#define QUEUE_DEPTH_COMM_PACKETS      8
#define QUEUE_DEPTH_CTRL_CMDS         5

/* 事件组句柄 (外部定义) */
extern EventGroupHandle_t g_system_event_group;

/* =============================================================================
 * 日志与诊断数据结构
 * ==========================================================================*/

/** @brief 日志条目 (vTaskLogger 通过 s_queue_log 发送) */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;      /* 时间戳 */
    uint8_t  level;             /* 0=DEBUG, 1=INFO, 2=WARN, 3=ERR */
    uint8_t  source_task;       /* 源任务ID */
    char     msg[128];           /* 日志消息 */
} log_entry_t;

/** @brief 诊断报告 (vTaskDiag 通过 s_queue_log 发送) */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;      /* 时间戳 */
    uint32_t free_heap;         /* 空闲堆内存 */
    uint16_t stack_watermarks[7]; /* 各任务堆栈水位 */
    float    cpu_load_pct;      /* CPU 使用率估算 (0.0-100.0) */
    uint32_t uptime_ms;         /* 系统运行时间 */
    uint8_t  error_count;       /* 累计错误计数 */
    uint8_t  active_tasks;      /* 活动任务数 */
} diag_report_t;

/* =============================================================================
 * 任务函数声明
 * ==========================================================================*/

/**
 * @brief  摄像头采集任务
 * @param  pvParameters: 任务参数
 */
void vTaskCamera(void *pvParameters);

/**
 * @brief  NPU 推理任务
 * @param  pvParameters: 任务参数
 */
void vTaskInference(void *pvParameters);

/**
 * @brief  通信任务 (SDIO with ESP32)
 * @param  pvParameters: 任务参数
 */
void vTaskComm(void *pvParameters);

/**
 * @brief  传感器任务 (ToF + UWB)
 * @param  pvParameters: 任务参数
 */
void vTaskSensor(void *pvParameters);

/**
 * @brief  主控逻辑任务
 * @param  pvParameters: 任务参数
 */
void vTaskControl(void *pvParameters);

/**
 * @brief  日志任务
 * @param  pvParameters: 任务参数
 */
void vTaskLogger(void *pvParameters);

/**
 * @brief  诊断任务
 * @param  pvParameters: 任务参数
 */
void vTaskDiag(void *pvParameters);

/* =============================================================================
 * 系统初始化函数
 * ==========================================================================*/

/**
 * @brief  创建所有 FreeRTOS 任务和同步原语
 * @retval LINGXI_OK 成功
 */
lingxi_err_t app_freertos_init(void);

/**
 * @brief  启动调度器
 * @note   此函数不返回
 */
void app_freertos_start(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_FREERTOS_H */
