/**
  ******************************************************************************
  * @file    app_freertos.c
  * @brief   FreeRTOS 任务模型实现
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 7个任务: Camera, Inference, Comm, Sensor, Control, Logger, Diag
  * - 使用队列、信号量、事件组进行任务间同步
  * - 内存分配全部静态或从 FreeRTOS heap
  ******************************************************************************
  */

#include "app_freertos.h"
#include "bsp_mipi_csi.h"
#include "bsp_tof.h"
#include "bsp_uwb.h"
#include "bsp_sdio.h"
#include "app_npu.h"
#include "app_fusion.h"
#include "app_control.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

/* =============================================================================
 * 全局同步原语
 * ==========================================================================*/
EventGroupHandle_t g_system_event_group = NULL;

static QueueHandle_t s_queue_cam_frames = NULL;
static QueueHandle_t s_queue_infer_results = NULL;
static QueueHandle_t s_queue_sensor_data = NULL;
static QueueHandle_t s_queue_ctrl_cmds = NULL;

/* 日志/诊断队列 */
static QueueHandle_t s_queue_log = NULL;

/* 任务句柄 (用于 vTaskDiag 采集堆栈水位) */
TaskHandle_t xCameraTaskHandle = NULL;
TaskHandle_t xInferenceTaskHandle = NULL;
TaskHandle_t xCommTaskHandle = NULL;
TaskHandle_t xSensorTaskHandle = NULL;
TaskHandle_t xControlTaskHandle = NULL;
TaskHandle_t xLoggerTaskHandle = NULL;
TaskHandle_t xDiagTaskHandle = NULL;

static SemaphoreHandle_t s_mutex_npu = NULL;
static SemaphoreHandle_t s_mutex_sdram = NULL;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void system_event_group_init(void);
static void queues_init(void);
static void mutexes_init(void);

/* =============================================================================
 * 任务实现
 * ==========================================================================*/

/**
 * @brief  摄像头采集任务
 * @note   最高优先级, 周期性从 MIPI CSI-2 获取帧
 */
void vTaskCamera(void *pvParameters)
{
    (void)pvParameters;

    LX_DEBUG_PRINT("vTaskCamera started");

    /* 等待系统初始化完成 */
    xEventGroupWaitBits(g_system_event_group, EVT_SYS_INIT_DONE,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    /* 启动摄像头 */
    bsp_mipi_csi_start();

    uint32_t last_wake_time = xTaskGetTickCount();

    for (;;) {
        /* 周期性延迟 */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TASK_PERIOD_CAMERA));

        /* 获取帧 */
        uint8_t *frame = bsp_mipi_csi_get_frame();
        if (frame == NULL) {
            continue; /* 超时或错误 */
        }

        /* 发送到推理队列 */
        if (xQueueSend(s_queue_cam_frames, &frame, 0) != pdTRUE) {
            /* 队列满, 释放帧 */
            bsp_mipi_csi_release_frame(frame);
        }
    }
}

/**
 * @brief  NPU 推理任务
 * @note   从队列获取帧, 执行 NPU 推理, 输出结果
 */
void vTaskInference(void *pvParameters)
{
    (void)pvParameters;

    LX_DEBUG_PRINT("vTaskInference started");

    xEventGroupWaitBits(g_system_event_group, EVT_SYS_INIT_DONE,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    uint8_t *frame = NULL;
    npu_infer_result_t infer_result = {0};
    uint32_t last_wake_time = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TASK_PERIOD_INFERENCE));

        /* 从队列获取帧 */
        if (xQueueReceive(s_queue_cam_frames, &frame, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue; /* 无新帧 */
        }

        /* NPU 推理 (互斥访问) */
        if (xSemaphoreTake(s_mutex_npu, pdMS_TO_TICKS(100)) == pdTRUE) {
            /* 预处理 */
            app_npu_preprocess(frame, CAM_WIDTH, CAM_HEIGHT);

            /* 执行推理 */
            lingxi_err_t err = app_npu_run(&infer_result);
            if (err == LINGXI_OK) {
                /* 发送推理结果 */
                xQueueSend(s_queue_infer_results, &infer_result, 0);

                /* 设置事件标志 */
                if (infer_result.obstacle_detected) {
                    xEventGroupSetBits(g_system_event_group, EVT_OBSTACLE_DETECTED);
                }
                if (infer_result.edge_detected) {
                    xEventGroupSetBits(g_system_event_group, EVT_EDGE_DETECTED);
                }
            }

            xSemaphoreGive(s_mutex_npu);
        }

        /* 释放帧缓冲区 */
        bsp_mipi_csi_release_frame(frame);
    }
}

/**
 * @brief  通信任务
 * @note   从控制队列获取指令 + 日志数据, 经 SDIO 发送至 ESP32-C6
 *         不再直接消费推理/传感器队列 (由 vTaskControl 透传)
 */
void vTaskComm(void *pvParameters)
{
    (void)pvParameters;

    LX_DEBUG_PRINT("vTaskComm started");

    xEventGroupWaitBits(g_system_event_group, EVT_SYS_INIT_DONE,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    sdio_packet_t pkt = {0};
    control_cmd_t ctrl_cmd = {0};
    log_entry_t log_entry = {0};

    uint32_t last_wake_time = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TASK_PERIOD_COMM));

        /* 1. 发送控制指令 (最高优先级) */
        if (xQueueReceive(s_queue_ctrl_cmds, &ctrl_cmd, 0) == pdTRUE) {
            pkt.magic = 0xA5;
            pkt.type = SDIO_PKT_CTRL_CMD;
            pkt.seq++;
            pkt.len = sizeof(control_cmd_t);
            memcpy(pkt.data, &ctrl_cmd, sizeof(control_cmd_t));
            bsp_sdio_send_packet(&pkt, 50);
        }

        /* 2. 发送日志/诊断数据 (低优先级, 剩余带宽) */
        if (s_queue_log != NULL &&
            xQueueReceive(s_queue_log, &log_entry, 0) == pdTRUE) {
            pkt.magic = 0xA5;
            pkt.type = SDIO_PKT_LOG;
            pkt.seq++;
            pkt.len = sizeof(log_entry_t);
            memcpy(pkt.data, &log_entry, sizeof(log_entry_t));
            bsp_sdio_send_packet(&pkt, 50);
        }
    }
}

/**
 * @brief  传感器任务
 * @note   读取 ToF 和 UWB 数据, 执行传感器融合
 */
void vTaskSensor(void *pvParameters)
{
    (void)pvParameters;

    LX_DEBUG_PRINT("vTaskSensor started");

    xEventGroupWaitBits(g_system_event_group, EVT_SYS_INIT_DONE,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    tof_measurement_t tof_meas = {0};
    uwb_ranging_data_t uwb_data = {0};
    sensor_fusion_data_t fusion_data = {0};

    uint32_t last_wake_time = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TASK_PERIOD_SENSOR));

        /* 读取 ToF */
        if (bsp_tof_get_measurement(&tof_meas) == LINGXI_OK) {
            fusion_data.tof_distance_mm = tof_meas.distance_mm;
            fusion_data.tof_valid = tof_meas.valid;
        }

        /* 读取 UWB */
        if (bsp_uwb_get_ranging_data(&uwb_data) == LINGXI_OK) {
            if (uwb_data.num_anchors > 0) {
                fusion_data.uwb_distance_m = uwb_data.anchors[0].distance_m;
                fusion_data.uwb_valid = uwb_data.anchors[0].valid;
            }
        }

        /* 传感器融合 */
        app_fusion_update(&fusion_data);

        /* 发送到通信队列 */
        xQueueSend(s_queue_sensor_data, &fusion_data, 0);

        /* 设置事件 */
        xEventGroupSetBits(g_system_event_group, EVT_SENSOR_DATA_READY);
    }
}

/**
 * @brief  主控逻辑任务
 * @note   避障决策、边缘识别、跟踪控制
 */
void vTaskControl(void *pvParameters)
{
    (void)pvParameters;

    LX_DEBUG_PRINT("vTaskControl started");

    xEventGroupWaitBits(g_system_event_group, EVT_SYS_INIT_DONE,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    control_state_t ctrl_state = {0};
    npu_infer_result_t infer_result = {0};
    sensor_fusion_data_t sensor_data = {0};

    uint32_t last_wake_time = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TASK_PERIOD_CONTROL));

        /* 获取推理结果 (非阻塞) */
        xQueueReceive(s_queue_infer_results, &infer_result, 0);

        /* 获取传感器数据 (非阻塞) */
        xQueueReceive(s_queue_sensor_data, &sensor_data, 0);

        /* 执行控制逻辑 */
        app_control_update(&ctrl_state, &infer_result, &sensor_data);

        /* 打包控制指令 + 遥测透传 */
        control_cmd_t cmd = {0};
        cmd.cmd_type = ctrl_state.current_mode;
        cmd.param1 = ctrl_state.target_distance_mm;
        cmd.param2 = ctrl_state.target_heading_deg;
        cmd.param3 = ctrl_state.target_speed_mmps;
        cmd.emergency_stop = ctrl_state.emergency_stop;
        cmd.timestamp_ms = xTaskGetTickCount();

        /* 遥测透传: 将传感器/推理数据打包进指令, vTaskComm 直接发送 */
        cmd.fused_dist_mm = sensor_data.fused_distance_mm;
        cmd.confidence = sensor_data.confidence;
        cmd.obstacle_flag = infer_result.obstacle_detected;
        cmd.edge_flag = infer_result.edge_detected;
        cmd.track_flag = infer_result.track_target_valid;
        cmd.inference_time_us = infer_result.inference_time_us;
        cmd.free_heap_bytes = xPortGetFreeHeapSize();

        xQueueSend(s_queue_ctrl_cmds, &cmd, 0);
    }
}

/**
 * @brief  日志任务
 * @note   异步日志: 高优先级任务通过 s_queue_log 提交日志, 本任务负责实际输出
 */
void vTaskLogger(void *pvParameters)
{
    (void)pvParameters;

    LX_DEBUG_PRINT("vTaskLogger started");

    xEventGroupWaitBits(g_system_event_group, EVT_SYS_INIT_DONE,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    log_entry_t entry = {0};

    uint32_t last_wake_time = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TASK_PERIOD_LOGGER));

        /* 消费日志队列, 通过 s_queue_log 发送至 vTaskComm */
        if (s_queue_log != NULL &&
            xQueueReceive(s_queue_log, &entry, 0) == pdTRUE) {
            /* entry 已由生产者填充, vTaskComm 会读取并发送 */
            /* 此处无需额外操作 */
        }
    }
}

/**
 * @brief  诊断任务
 * @note   系统健康检查: 堆/栈/CPU/看门狗, 通过 s_queue_log 上报
 */
void vTaskDiag(void *pvParameters)
{
    (void)pvParameters;

    LX_DEBUG_PRINT("vTaskDiag started");

    xEventGroupWaitBits(g_system_event_group, EVT_SYS_INIT_DONE,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    /* 任务句柄数组 (用于采集堆栈水位) */
    const TaskHandle_t task_handles[7] = {
        xCameraTaskHandle,
        xInferenceTaskHandle,
        xCommTaskHandle,
        xSensorTaskHandle,
        xControlTaskHandle,
        xLoggerTaskHandle,
        xDiagTaskHandle
    };

    diag_report_t report = {0};
    uint32_t last_wake_time = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TASK_PERIOD_DIAG));

        report.timestamp_ms = xTaskGetTickCount();
        report.free_heap = xPortGetFreeHeapSize();
        report.uptime_ms = xTaskGetTickCount();
        report.active_tasks = uxTaskGetNumberOfTasks();

        /* 采集各任务堆栈水位 */
        for (int i = 0; i < 7; i++) {
            if (task_handles[i] != NULL) {
                report.stack_watermarks[i] =
                    uxTaskGetStackHighWaterMark(task_handles[i]);
            }
        }

        /* 通过日志队列发送诊断报告 */
        if (s_queue_log != NULL) {
            log_entry_t entry;
            entry.timestamp_ms = report.timestamp_ms;
            entry.level = 1;  /* INFO */
            entry.source_task = 6; /* 诊断任务ID */
            snprintf(entry.msg, sizeof(entry.msg),
                     "DIAG:heap=%lu B,stack=[%u,%u,%u,%u,%u,%u,%u],tasks=%u",
                     (unsigned long)report.free_heap,
                     report.stack_watermarks[0],
                     report.stack_watermarks[1],
                     report.stack_watermarks[2],
                     report.stack_watermarks[3],
                     report.stack_watermarks[4],
                     report.stack_watermarks[5],
                     report.stack_watermarks[6],
                     (unsigned)report.active_tasks);

            xQueueSend(s_queue_log, &entry, 0);
        }

        /* 喂看门狗 */
        /* HAL_IWDG_Refresh(&hiwdg); */
    }
}

/* =============================================================================
 * 系统初始化
 * ==========================================================================*/

/**
 * @brief  初始化 FreeRTOS 任务和同步原语
 */
lingxi_err_t app_freertos_init(void)
{
    /* 创建事件组 */
    system_event_group_init();

    /* 创建队列 */
    queues_init();

    /* 创建互斥量 */
    mutexes_init();

    /* 创建任务 */
    BaseType_t ret;

    ret = xTaskCreate(vTaskCamera, "Camera", TASK_STACK_CAMERA, NULL,
                      TASK_PRIO_CAMERA, &xCameraTaskHandle);
    if (ret != pdPASS) return LINGXI_ERR_NO_MEM;

    ret = xTaskCreate(vTaskInference, "Inference", TASK_STACK_INFERENCE, NULL,
                      TASK_PRIO_INFERENCE, &xInferenceTaskHandle);
    if (ret != pdPASS) return LINGXI_ERR_NO_MEM;

    ret = xTaskCreate(vTaskComm, "Comm", TASK_STACK_COMM, NULL,
                      TASK_PRIO_COMM, &xCommTaskHandle);
    if (ret != pdPASS) return LINGXI_ERR_NO_MEM;

    ret = xTaskCreate(vTaskSensor, "Sensor", TASK_STACK_SENSOR, NULL,
                      TASK_PRIO_SENSOR, &xSensorTaskHandle);
    if (ret != pdPASS) return LINGXI_ERR_NO_MEM;

    ret = xTaskCreate(vTaskControl, "Control", TASK_STACK_CONTROL, NULL,
                      TASK_PRIO_CONTROL, &xControlTaskHandle);
    if (ret != pdPASS) return LINGXI_ERR_NO_MEM;

    ret = xTaskCreate(vTaskLogger, "Logger", TASK_STACK_LOGGER, NULL,
                      TASK_PRIO_LOGGER, &xLoggerTaskHandle);
    if (ret != pdPASS) return LINGXI_ERR_NO_MEM;

    ret = xTaskCreate(vTaskDiag, "Diag", TASK_STACK_DIAG, NULL,
                      TASK_PRIO_DIAG, &xDiagTaskHandle);
    if (ret != pdPASS) return LINGXI_ERR_NO_MEM;

    LX_DEBUG_PRINT("FreeRTOS tasks created: 7 tasks");
    return LINGXI_OK;
}

/**
 * @brief  启动调度器
 */
void app_freertos_start(void)
{
    /* 设置初始化完成标志 */
    xEventGroupSetBits(g_system_event_group, EVT_SYS_INIT_DONE);

    LX_DEBUG_PRINT("Starting FreeRTOS scheduler...");
    vTaskStartScheduler();

    /* 不应到达此处 */
    for (;;);
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

static void system_event_group_init(void)
{
    g_system_event_group = xEventGroupCreate();
    configASSERT(g_system_event_group != NULL);
}

static void queues_init(void)
{
    s_queue_cam_frames = xQueueCreate(QUEUE_DEPTH_CAM_FRAMES, sizeof(uint8_t*));
    configASSERT(s_queue_cam_frames != NULL);

    s_queue_infer_results = xQueueCreate(QUEUE_DEPTH_INFER_RESULTS, sizeof(npu_infer_result_t));
    configASSERT(s_queue_infer_results != NULL);

    s_queue_sensor_data = xQueueCreate(QUEUE_DEPTH_SENSOR_DATA, sizeof(sensor_fusion_data_t));
    configASSERT(s_queue_sensor_data != NULL);

    s_queue_ctrl_cmds = xQueueCreate(QUEUE_DEPTH_CTRL_CMDS, sizeof(control_cmd_t));
    configASSERT(s_queue_ctrl_cmds != NULL);

    s_queue_log = xQueueCreate(QUEUE_DEPTH_CTRL_CMDS, sizeof(log_entry_t));
    configASSERT(s_queue_log != NULL);
}

static void mutexes_init(void)
{
    s_mutex_npu = xSemaphoreCreateMutex();
    configASSERT(s_mutex_npu != NULL);

    s_mutex_sdram = xSemaphoreCreateMutex();
    configASSERT(s_mutex_sdram != NULL);
}

/* =============================================================================
 * 钩子函数
 * ==========================================================================*/

/**
 * @brief  内存分配失败钩子
 */
void vApplicationMallocFailedHook(void)
{
    LX_ERR_PRINT("Malloc failed!");
    taskDISABLE_INTERRUPTS();
    for (;;);
}

/**
 * @brief  堆栈溢出钩子
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    LX_ERR_PRINT("Stack overflow: %s", pcTaskName);
    taskDISABLE_INTERRUPTS();
    for (;;);
}

/**
 * @brief  空闲钩子
 */
void vApplicationIdleHook(void)
{
    /* 可进入低功耗模式 */
    /* __WFI(); */
}
