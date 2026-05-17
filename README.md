# 灵犀智空 Lingxi BL 感知模组 — 固件工程

> **当前版本: v4.0** | 2026-05-17

## 工程概述

VSCode + Linux 环境下的跨平台固件工程，支持 STM32N6（主控，Cortex-M55 @ 800MHz）和 ESP32-C6（通信协处理器）。

### 核心能力

- **实时操作系统**: FreeRTOS V10.6.2 (ARM Cortex-M55 移植层)
- **控制算法**: 三段式 PID + 前馈补偿 + Slew Rate Limiter
- **传感器融合**: 卡尔曼滤波 (无锁双缓冲) — ToF/UWB/视觉
- **NPU 推理**: X-CUBE-AI 双模式支持 (存根/NPU加速)
- **通信**: SDIO 自定义协议 (控制指令 + 遥测透传)
- **诊断**: 实时堆/栈/CPU 监控, 异步日志

## 快速构建

```bash
cd stm32
make clean && make -j4
```

输出: `build/lingxi-bl-firmware.bin` (3.4KB)

## 目录结构

```
lingxi-bl-firmware/
├── stm32/                # STM32N657 主控固件
│   ├── Core/             # 核心代码 + FreeRTOS 头文件
│   │   └── Inc/FreeRTOSConfig.h
│   ├── Drivers/          # STM32N6xx HAL 驱动
│   ├── Middlewares/FreeRTOS/  # FreeRTOS 内核 V10.6.2
│   ├── App/
│   │   ├── inc/          # 应用头文件
│   │   └── src/          # 应用源码
│   │       ├── app_control.c   625行 (新建)
│   │       ├── app_fusion.c    342行 (重构)
│   │       ├── app_freertos.c  536行 (重构)
│   │       └── app_npu.c       496行 (双模式)
│   ├── Makefile          # 构建系统
│   └── build/            # 编译输出
├── esp32/                # ESP32-C6 通信固件
├── shared/               # 共享代码
├── docs/                 # 文档
│   └── software_design.md    # 软件设计文档 (v4.0)
└── README.md
```

## 系统架构

```
┌─────────┐   ┌───────────┐   ┌──────────┐   ┌────────┐   ┌─────────┐
│Camera   │──▶│Inference  │──▶│Control   │──▶│Comm    │──▶│SDIO     │
│(60fps)  │   │(30fps NPU)│   │(50Hz PID)│   │(100Hz) │   │→ ESP32  │
└─────────┘   └───────────┘   └──────────┘   └────────┘   └─────────┘
                                    ▲
┌─────────┐   ┌───────────┐        │
│Sensor   │──▶│Fusion     │────────┘
│(30Hz)   │   │(Kalman)   │
└─────────┘   └───────────┘

┌─────────┐
│Diag     │──▶[Log Queue]──▶Comm
│(0.2Hz)  │
└─────────┘
```

## 文档

- `docs/software_design.md` — 完整软件设计文档 (推荐)
- `docs/dev_environment.md` — 开发环境搭建
- `docs/bootloader_design.md` — 引导加载程序设计
- `docs/test_framework.md` — 测试框架

## 版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v4.0 | 2026-05-17 | PID+前馈, 双缓冲, FreeRTOS 集成, 零警告编译 |
| v3.2 | 2026-05-15 | 初始 HAL 层架构 |
