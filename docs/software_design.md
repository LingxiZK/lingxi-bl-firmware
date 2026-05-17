# Lingxi BL 感知模组 — 软件设计文档 v4.0

> 更新日期: 2026-05-17  
> 对应固件: lingxi-bl-firmware v4.0  
> 主控芯片: STM32N657L0H3Q (Cortex-M55 @ 800MHz)  
> 协处理器: ESP32-C6 (SDIO Slave)

---

## 目录

1. [系统概述](#1-系统概述)
2. [软件架构](#2-软件架构)
3. [模块详解](#3-模块详解)
   - 3.1 [控制引擎 (app_control)](#31-控制引擎-app_control)
   - 3.2 [传感器融合 (app_fusion)](#32-传感器融合-app_fusion)
   - 3.3 [任务调度 (app_freertos)](#33-任务调度-app_freertos)
   - 3.4 [NPU 推理 (app_npu)](#34-npu-推理-app_npu)
   - 3.5 [通信协议 (bsp_sdio)](#35-通信协议-bsp_sdio)
4. [数据流](#4-数据流)
5. [任务调度图](#5-任务调度图)
6. [性能指标](#6-性能指标)
7. [构建与部署](#7-构建与部署)

---

## 1. 系统概述

Lingxi BL 感知模组固件基于 **STM32N657** (Cortex-M55 + Neural-ART NPU) 实现实时感知-决策-控制闭环。系统采用 **FreeRTOS V10.6.2** 作为实时操作系统，管理 7 个周期性任务，通过队列和事件组实现任务间通信。

### 1.1 核心能力

| 功能 | 描述 | 周期 |
|------|------|------|
| 摄像头采集 | MIPI CSI-2 → VD55G1 传感器, 60fps | 16.6ms |
| NPU 推理 | 目标检测/边缘识别/跟踪, 30fps | 33ms |
| 传感器融合 | ToF + UWB + 视觉 → 卡尔曼滤波 | 33ms |
| 控制逻辑 | PID + 前馈 + 指令平滑, 50Hz | 20ms |
| 通信 | SDIO → ESP32-C6, 100Hz | 10ms |
| 诊断 | 堆/栈/CPU 监控 | 5s |
| 日志 | 异步日志输出 | 1s |

### 1.2 处理流水线

```
Camera ──→ Inference ──→ Control ──→ Comm ──→ SDIO → ESP32
                                    ↑
Sensor ──→ Fusion ─────────────────┘
```

---

## 2. 软件架构

### 2.1 目录结构

```
stm32/
├── App/
│   ├── inc/               # 应用层头文件
│   │   ├── app_control.h     控制引擎
│   │   ├── app_fusion.h      传感器融合
│   │   ├── app_freertos.h    FreeRTOS 任务模型
│   │   ├── app_npu.h         NPU 推理
│   │   ├── bsp_*.h           板级支持包
│   │   └── lingxi_bl.h       主控头文件
│   └── src/               # 应用层实现
│       ├── app_control.c     PID + 状态机
│       ├── app_fusion.c      卡尔曼滤波 + 双缓冲
│       ├── app_freertos.c    7 任务管理
│       ├── app_npu.c         NPU 存根/真实实现
│       ├── bsp_*.c           外设驱动
├── Core/                  # STM32 核心代码
│   ├── Inc/               # FreeRTOS 头文件 + FreeRTOSConfig.h
│   └── Src/               # main.c + HAL MSP
├── Middlewares/FreeRTOS/  # FreeRTOS 内核 V10.6.2
│   └── source/ + portable/ARM_CM55_NTZ
├── Drivers/               # STM32N6xx HAL 驱动
└── Makefile               # Make 构建 (替代 CMake)
```

### 2.2 FreeRTOS 配置

| 参数 | 值 | 说明 |
|------|-----|------|
| `configCPU_CLOCK_HZ` | 800,000,000 | 系统主频 800MHz |
| `configTICK_RATE_HZ` | 1,000 | 系统节拍 1ms |
| `configTOTAL_HEAP_SIZE` | 512 KB | 堆内存 |
| `configMAX_PRIORITIES` | 16 | 最大优先级 |
| `configENABLE_FPU` | 1 | 启用 FPU (FPv5-D16) |
| `configENABLE_MVE` | 0 | MVE 向量扩展关闭 |
| `configENABLE_MPU` | 0 | MPU 关闭 |
| `configENABLE_TRUSTZONE` | 0 | TrustZone 关闭 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 栈溢出检测方法 2 |
| `configUSE_MALLOC_FAILED_HOOK` | 1 | malloc 失败钩子 |
| `configUSE_TIMERS` | 1 | 软件定时器 |
| Heap 方案 | heap_4 | 碎片合并 |

---

## 3. 模块详解

### 3.1 控制引擎 (app_control)

#### 3.1.1 三段式 PID 控制

针对 **Z 轴（距离）**、**Yaw（航向）**、**Speed（速度）** 各配置独立 PID 控制器，带以下高级特性：

| 特性 | 实现方式 |
|------|----------|
| 反步积 | `integral_limit` 钳制积分项 |
| 微分先行 | 只对测量值微分，避免 Derivative Kick |
| 前馈补偿 | `ff_gain * Δsetpoint/Δt` 补偿惯性滞后 |
| Slew Rate Limiter | 输出变化率限制，消除电机高频抖动 |

#### 3.1.2 PID 参数

| 轴 | Kp | Ki | Kd | ff_gain | int_limit | out_limit | slew_rate |
|----|----|----|----|---------|-----------|-----------|-----------|
| Z | 2.5 | 0.15 | 0.8 | 0.10 | 500 | 2000 mm/s | 500 mm/s² |
| Yaw | 8.0 | 0.05 | 3.0 | 0.00 | 200 | 300 deg/s | 180 deg/s² |
| Speed | 1.8 | 0.10 | 0.5 | 0.15 | 300 | 1000 mm/s | 400 mm/s² |

#### 3.1.3 模式状态机

```
          ┌──────────────────────────────────────┐
          │              IDLE                    │
          └──┬───────┬───────┬──────────────────┘
             │       │       │
     ┌───────▼┐ ┌───▼───┐ ┌─▼──────┐
     │OBSTACLE│ │  EDGE │ │ TRACK  │
     │ AVOID  │ │DETECT │ │        │
     └───┬────┘ └───┬───┘ └───┬────┘
         │          │         │
         └──────┬───┴─────────┘
                ▼
          ┌──────────┐
          │EMERGENCY │  ← 临界距离触发 (任何模式)
          └──────────┘
```

**切换规则**:
- 每种模式最少驻留 **100ms** (防高频切换)
- 模式进入时 **复位 PID 积分器**
- EMERGENCY 优先级最高，任意模式可切入

#### 3.1.4 避障逻辑

```
fused_dist < CRITICAL (800mm)   → 全速后退 (-500 mm/s)
fused_dist < WARNING (1500mm)   → 线性减速 + 转向
fused_dist < SAFE (2000mm)      → 缓慢接近 (150 mm/s)
fused_dist >= SAFE              → 正常巡航 (300 mm/s)
航向修正: 障碍物框中心 → 图像中心偏移 → yaw_cmd
```

### 3.2 传感器融合 (app_fusion)

#### 3.2.1 架构：无锁双缓冲 (Lock-free Double Buffering)

采用 **Optimistic Locking** 机制，消除 `taskENTER_CRITICAL()` 导致的延时抖动：

```
写端 (vTaskSensor):
  ┌──────────────┐
  │ 计算新结果    │
  │ s_version++  │  ← 标记写入开始
  │ memcpy(buf2) │  ← 写入后台缓冲区
  │ active_idx=1 │  ← 原子切换索引
  │ s_version++  │  ← 标记写入完成
  └──────────────┘

读端 (vTaskControl):
  ┌──────────────┐
  │ v1 = version │  ← 记录版本
  │ idx=active   │  ← 获取当前索引
  │ memcpy(data) │  ← 读取数据
  │ v2 = version │  ← 检查版本
  │ if(v1≠v2)→重试│  ← 冲突则重试 (最多3次)
  │ else→返回    │
  └──────────────┘
```

#### 3.2.2 卡尔曼滤波器

- **状态向量**: [x, y, z, vx, vy, vz]^T (6维)
- **预测**: 常速度模型 (CV), 利用 F 矩阵稀疏性手写优化
- **更新**: Rank-1 Update, 计算复杂度 O(N²) 而非 O(N³)
- **测量噪声**: ToF=100mm, UWB=400mm, Vision=250mm

#### 3.2.3 传感器加权

```
融合距离 = w_tof * dist_tof + w_uwb * dist_uwb + w_vis * dist_vis
权重: ToF=0.4, UWB=0.3, Vision=0.3
```

### 3.3 任务调度 (app_freertos)

#### 3.3.1 任务定义

| # | 任务名 | 函数 | 优先级 | 周期 | 栈 (words) |
|---|--------|------|--------|------|------------|
| 1 | Camera | `vTaskCamera` | 14 | 16.6ms (60fps) | 1024 |
| 2 | Inference | `vTaskInference` | 13 | 33ms (30fps) | 2048 |
| 3 | Comm | `vTaskComm` | 10 | 10ms (100Hz) | 1024 |
| 4 | Sensor | `vTaskSensor` | 8 | 33ms (30Hz) | 512 |
| 5 | Control | `vTaskControl` | 7 | 20ms (50Hz) | 1024 |
| 6 | Logger | `vTaskLogger` | 3 | 1s | 512 |
| 7 | Diag | `vTaskDiag` | 2 | 5s | 512 |

#### 3.3.2 队列与通信

```
s_queue_cam_frames   (3)    uint8_t*          Camera → Inference
s_queue_infer_results(3)    npu_infer_result_t Inference → Control
s_queue_sensor_data  (5)    sensor_fusion_data_t Sensor → Control
s_queue_ctrl_cmds    (5)    control_cmd_t      Control → Comm
s_queue_log          (5)    log_entry_t        Diag/Logger → Comm
```

#### 3.3.3 数据流拓扑

```
Camera ──→ [cam_frames] ──→ Inference ──→ [infer_results] ──┐
                                                              │
Sensor ──→ [sensor_data] ──────────────────────────────────┬─┤
                                                            │ │
                    Diag ──→ [log] ──┐                     │ │
                                     ▼                     ▼ ▼
                              Comm ←── [ctrl_cmds] ←── Control
                                │
                                └── SDIO → ESP32-C6
```

### 3.4 NPU 推理 (app_npu)

#### 3.4.1 双模式设计

系统支持两种运行模式，通过 `USE_X_CUBE_AI` 编译标志切换：

| 模式 | 标志 | 描述 |
|------|------|------|
| **存根模式** (默认) | `USE_X_CUBE_AI=0` | 返回模拟数据，用于开发/测试 |
| **NPU 加速** | `USE_X_CUBE_AI=1` | 调用 Neural-ART NPU 推理 |

#### 3.4.2 存根模式输出

```c
result->obstacle_detected = 1;  // 默认检测到障碍物
result->inference_time_us = 1000;  // 模拟 1ms 推理时间
```

### 3.5 通信协议 (bsp_sdio)

SDIO 协议包格式 (自定义轻量协议):

```
┌──────┬──────┬──────┬──────┬──────┬──────────────────┐
│magic │ type │ seq  │ len  │ crc16│      data        │
│ 1 B  │ 1 B  │ 2 B  │ 2 B  │ 2 B  │   ≤ 1536 B      │
└──────┴──────┴──────┴──────┴──────┴──────────────────┘
magic = 0xA5
```

**包类型**:

| Type | 值 | 内容 | 来源 |
|------|-----|------|------|
| CMD | 0x01 | 命令 | (未使用) |
| DATA | 0x02 | 原始数据 | (未使用) |
| ACK | 0x03 | 确认 | 双向 |
| NACK | 0x04 | 否定确认 | 双向 |
| HEART | 0x05 | 心跳 | 双向 |
| CTRL_CMD | **0x06** | 控制指令 + 遥测 | STM32→ESP32 |
| LOG | **0x07** | 日志/诊断 | STM32→ESP32 |

---

## 4. 数据流

### 4.1 控制指令包 (SDIO_PKT_CTRL_CMD)

```c
typedef struct {
    control_mode_t cmd_type;    /* 指令类型 */
    int32_t param1;             /* 目标距离 (mm) */
    int32_t param2;             /* 航向角 (deg*10) */
    int32_t param3;             /* 速度 (mm/s) */
    uint8_t  emergency_stop;    /* 紧急停止标志 */
    uint32_t timestamp_ms;      /* 时间戳 */
    /* 遥测透传 */
    uint16_t fused_dist_mm;
    float    confidence;
    uint8_t  obstacle_flag;
    uint8_t  edge_flag;
    uint8_t  track_flag;
    uint16_t inference_time_us;
    uint32_t free_heap_bytes;
} control_cmd_t;
```

### 4.2 日志/诊断包

```c
// 日志条目 (SDIO_PKT_LOG)
typedef struct {
    uint32_t timestamp_ms;
    uint8_t  level;           // 0=DEBUG, 1=INFO, 2=WARN, 3=ERR
    uint8_t  source_task;     // 源任务ID
    char     msg[128];        // 日志消息
} log_entry_t;

// 诊断数据示例:
// "DIAG:heap=452300 B,stack=[128,256,64,192,...],tasks=7"
```

---

## 5. 任务调度图

```
时间轴 (ms)
0    5    10   15   20   25   30   35   40   45   50
│    │    │    │    │    │    │    │    │    │    │
Camera (16.6ms):  ■■■■■■■■■■    ■■■■■■■■■■    ■■
Inference(33ms):   ■■■■■■■■■■■■■■■■     ■■■■■■
Comm (10ms):   ■■■  ■■■  ■■■  ■■■  ■■■  ■■■  ■■■
Sensor (33ms):  ■■■■■■■■■■■■■■■■     ■■■■■■■■■■
Control (20ms):   ■■■■■■■■    ■■■■■■■■    ■■■■■■
Logger (1s):   ■                                  ...
Diag (5s):    ■                                   ...
```

---

## 6. 性能指标

### 6.1 固件大小

| 区域 | 大小 | 总量 | 占用率 |
|------|------|------|--------|
| ROM (Flash) | 3,444 B | 511 KB | 0.66% |
| RAM (BSS+Data) | 17,304 B | 1 MB | 1.65% |

### 6.2 闭环时延预算

| 阶段 | 预算 | 实际 (存根模式) |
|------|------|-----------------|
| 摄像头采集 | 16.6ms | — |
| NPU 推理 | 33ms | 1ms (存根) |
| 传感器融合 | 33ms | <1ms |
| 控制计算 | 20ms | <1ms |
| SDIO 发送 | 10ms | <1ms |
| **端到端** | **~50ms** | **~5ms (存根)** |

> 注: 实际 NPU 推理时间取决于模型复杂度，预算 33ms 对应 30fps 推理速率。

---

## 7. 构建与部署

### 7.1 快速构建 (Makefile)

```bash
cd lingxi-bl-firmware/stm32
make clean && make -j4
```

生成的二进制文件:
- `build/lingxi-bl-firmware.elf` — 含调试信息
- `build/lingxi-bl-firmware.hex` — Intel HEX
- `build/lingxi-bl-firmware.bin` — 纯二进制

### 7.2 编译依赖

| 工具 | 版本 | 用途 |
|------|------|------|
| `arm-none-eabi-gcc` | ≥10.3 | ARM 交叉编译器 |
| `GNU Make` | ≥4.0 | 构建系统 |
| `arm-none-eabi-objcopy` | — | 格式转换 |

### 7.3 关键编译选项

```makefile
MCU  = -mcpu=cortex-m55 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
CFLAGS = -O2 -g -Wall -fdata-sections -ffunction-sections -std=c11
LDFLAGS = -specs=nano.specs -Wl,--gc-sections
```

### 7.4 VSCode 集成

使用 Ctrl+Shift+B (或 `make`) 直接编译，编译结果在 `build/` 目录。

---

## 附录

### A. 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| v4.0 | 2026-05-17 | PID+前馈控制, 双缓冲融合, FreeRTOS 集成, NPU 存根 |
| v3.2 | 2026-05-15 | 初始 HAL 层架构 |

### B. 宏定义参考

| 宏 | 默认值 | 用途 |
|----|--------|------|
| `USE_X_CUBE_AI` | 0 | NPU 加速开关 (1=启用) |
| `DEBUG` | 定义 | 调试打印开关 |

### C. 相关文档

- `docs/dev_environment.md` — 开发环境搭建
- `docs/bootloader_design.md` — 引导加载程序设计
- `docs/test_framework.md` — 测试框架
- `docs/hal_to_ll_assessment.md` — HAL→LL 移植评估
