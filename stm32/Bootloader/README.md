# Lingxi N6 AI Deck — Bootloader (v1.1.0)

## 概述

针对 **STM32N657 + W25Q512 OctoSPI XIP** 架构的两级 Bootloader，支持通过无人机 UART 在线固件升级。

**关键修正 (v1.1.0):**
- STM32N6 **没有用户可写的 Internal Flash**。`0x08000000` 是 128KB 只读 Boot ROM。
- **所有代码（Bootloader + App）必须放在外部 XIP Flash（W25Q512）中**。
- Boot ROM 上电后从 XIP Flash 加载 Bootloader 到 RAM 执行。
- Bootloader 运行时通过 XSPI 操作 W25Q512 实现 OTA 升级。

---

## STM32N6 启动模式 (RM0486)

| BOOT0 | BOOT1 | 启动源 | 说明 |
|:-----:|:-----:|--------|------|
| X | **1** | **Development boot** | 安全调试循环，开发调试用 |
| **0** | **0** | **Flash boot** | 从 XIP Flash 加载固件 ← **正常运行模式** |
| **1** | **0** | **Serial boot** | 内置 System Bootloader，串口/USB 下载 |

**正常运行:** `BOOT0=0, BOOT1=0` → Boot ROM 从 W25Q512 加载 Bootloader → Bootloader 加载 App

**首次烧录:** `BOOT0=1, BOOT1=0` → 用 STM32CubeProgrammer 通过 UART 把 Bootloader 烧到 W25Q512

---

## 分区方案

```
W25Q512 XIP Flash (64MB @ 0x70000000):

0x7000_0000 — 0x7000_03FF  Bootloader Image Header  (1KB)
0x7000_0400 — 0x7001_FFFF  Bootloader Code            (~124KB)
0x7002_0000 — 0x7002_3FFF  Info Sector                (16KB)
0x7002_4000 — 0x7017_FFFF  Reserved
0x7018_0000 — 0x7018_03FF  App A Image Header         (1KB)
0x7018_0400 — 0x7027_FFFF  App A Code                 (~1MB)
0x7028_0000 — 0x7028_03FF  App B Image Header         (1KB)
0x7028_0400 — 0x7037_FFFF  App B Code                 (~1MB)
0x7038_0000 — ...          后续保留/数据
```

> **镜像头 (Image Header):** 每个可执行镜像前 1KB 由 `STM32_SigningTool_CLI` 生成，包含加载地址、入口点、安全属性等。开发阶段可不签名，只生成头。

---

## 文件结构

```
Bootloader/
├── Inc/
│   ├── bl_config.h      # 分区地址、配置常量
│   ├── bl_protocol.h    # UART OTA 协议定义
│   └── bl_flash.h       # XSPI Flash API
├── Src/
│   ├── bl_main.c        # 启动流程、跳转逻辑
│   ├── bl_protocol.c    # 帧解析、OTA 状态机
│   ├── bl_flash.c       # W25Q512 操作、CRC32、Info Sector
│   └── STM32N657X0HXQ_BOOTLOADER.ld  # Bootloader 链接脚本
└── README.md
```

---

## 启动流程

1. **Boot ROM 从 XIP Flash (0x70000000) 加载 Bootloader 到 RAM**
2. Bootloader 初始化 RCC / GPIO / UART1 / IWDG
3. 初始化 OctoSPI 控制器，使能 XIP
4. 通过 XSPI 读取 W25Q512 中的 Info Sector，确定激活分区
5. **3s 超时监听**—收到 `ENTER_BOOT(0x01)` 则进入升级模式
6. 超时后校验并跳转到 XIP App (A 或 B)

---

## 烧录方法

### 首次烧录 Bootloader

```bash
# 1. 设置 BOOT0=1, BOOT1=0 (Serial boot 模式)
# 2. 用 STM32CubeProgrammer 通过 UART 连接

# 生成镜像头（开发阶段，不签名）
STM32_SigningTool_CLI.exe -bin bootloader.bin -nk -of 0x80000000 -t fsbl \
  -o bootloader-trusted.bin -hv 2.3 -dump bootloader-trusted.bin

# 烧录到 XIP Flash 起始地址
STM32CubeProgrammer -c port=COM3 -w bootloader-trusted.bin 0x70000000

# 3. 设置 BOOT0=0, BOOT1=0，复位即从 Bootloader 启动
```

### 烧录 App

```bash
# 生成 App 镜像头
STM32_SigningTool_CLI.exe -bin app.bin -nk -of 0x80000000 -t appli \
  -o app-trusted.bin -hv 2.3 -dump app-trusted.bin

# 烧录到 App A 区域
STM32CubeProgrammer -c port=COM3 -w app-trusted.bin 0x70180000
```

---

## UART OTA 协议

帧格式: `[SOF:1=0xAA] [LEN:2 LE] [CMD:1] [SEQ:1] [PAYLOAD:N] [CRC16:2 LE]`

| 命令 | 码 | 说明 |
|-------|-----|------|
| ENTER_BOOT | 0x01 | 进入 Bootloader |
| GET_STATUS | 0x10 | 获取状态 |
| OTA_START | 0x60 | 开始升级（带 size/crc32/version） |
| OTA_CHUNK | 0x62 | 发送数据块（offset + data） |
| OTA_VERIFY | 0x64 | 校验固件 CRC |
| OTA_COMMIT | 0x66 | 提交并切换分区、复位 |
| OTA_ROLLBACK | 0x68 | 回滚到上一版本 |
| REBOOT | 0x82 | 重启 |

### OTA 流程

1. 无人机发送 `OTA_START`（目标分区、固件大小、CRC32）
2. Bootloader 擦除目标分区（App A 或 B）
3. 无人机分块发送 `OTA_CHUNK`，Bootloader 先写入 **RAM 缓存**
4. 全部接收完后，从 RAM 缓存写入 W25Q512
5. 发送 `OTA_VERIFY` 校验
6. 发送 `OTA_COMMIT` 切换激活分区并复位

> **注意:** OTA 接收过程使用 RAM 缓存（1MB AXISRAM），不是 Flash。因为 N6 没有用户 Internal Flash。

---

## 与无人机的集成

### 链路
无人机主控(如 Crazyflie STM32F405) → UART → Deck(STM32N6 UART1 PA9/PA10)

### 推荐接口
在无人机固件中新增一个 CRTP 端口或直接通过 UART 指令：
```c
// 无人机端示例（Python / C）
send_ota_start(partition=1, size=512000, crc32=0xAABBCCDD, version="3.3.0")
for chunk in firmware_chunks:
    send_ota_chunk(offset, chunk_data)
verify = send_ota_verify()
if verify.ok:
    send_ota_commit()  // Deck 自动复位启动新固件
```

---

## 安全机制

1. **A/B 双备份**—升级失败可回滚
2. **CRC32 校验**—每块 + 整体校验
3. **Info Sector 原子更新**—先写新分区，再切换标志
4. **看门狗保护**—OTA 过程中超时自动复位

---

## 已知限制

1. **开发阶段需手动处理镜像头**—`STM32_SigningTool_CLI` 生成 1KB header，烧录时与 `.bin` 拼接
2. **Bootloader 自身升级**—需要 Boot ROM 的 Serial boot 模式重新烧录（或通过特殊 OTA 流程）
3. **XSPI 配置**—`bl_xspi_init()` 中的参数需要根据实际硬件（CS 引脚、时钟极性等）用 CubeMX 调整
