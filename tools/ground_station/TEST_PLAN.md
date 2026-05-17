# Lingxi BL 感知模组 — Phase 1 端到端视频流回传验证计划

> **目标：** 验证 VD55G1 画面通过 "MIPI → SDRAM → 下采样 → SDIO → ESP32 → Wi-Fi → 地面站" 链路的端到端通断、延迟与稳定性。
>
> **测试时间：** 硬件就绪后执行（预计 ~2 小时）
>
> **测试人：** ________________
>
> **测试日期：** ________________

---

## 一、前置条件

### 1.1 硬件清单

| # | 组件 | 状态 | 备注 |
|---|------|------|------|
| 1 | STM32N6 目标板（含 VD55G1 + IS42SM16320E SDRAM） | ☐ |  |
| 2 | ESP32-C6 模块 | ☐ | 与 STM32N6 通过 SDIO 连接 |
| 3 | FTDI/调试串口线（STM32N6 USART1） | ☐ | 115200 8N1 |
| 4 | 5V 电源（USB-C 或 J-Link 供电） | ☐ |  |
| 5 | PC（运行地面站软件） | ☐ | 同一 Wi-Fi 网络 |
| 6 | Wi-Fi AP/路由器 | ☐ | ESP32-C6 STA 模式连接 |
| 7 | USB 相机（可选，用于硬件未就绪时模拟 VD55G1） | ☐ | 开发阶段替代 |

### 1.2 软件环境

| # | 组件 | 版本 | 状态 |
|---|------|------|------|
| 1 | `ground_station.py` | v1.0 | ☐ |
| 2 | OpenCV (`cv2`) | ≥4.5 | ☐ `pip install opencv-python` |
| 3 | STM32 固件（编译通过） | lingxi-bl-firmware | ☐ |
| 4 | ESP32-C6 固件 | ESP-IDF ≥5.0 | ☐ |

### 1.3 网络配置

| 参数 | 值 | 备注 |
|------|-----|------|
| Wi-Fi SSID | ________________ | ESP32 连接的 AP |
| Wi-Fi 密码 | ________________ | |
| 地面站 IP | ________________ | `ip a` 查看 |
| 地面站端口 | 5566（UDP） | `ground_station.py --port 5566` |

### 1.4 编译与烧录

```bash
# STM32N6
cd /media/guotong/DATA/Lingxi_Project/Lingxi_N6_AI_Deck/lingxi-bl-firmware/stm32
make clean && make
# 烧录: 通过 J-Link 或 ST-Link

# ESP32-C6
cd /media/guotong/DATA/Lingxi_Project/Lingxi_N6_AI_Deck/lingxi-bl-firmware/esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## 二、测试用例

---

### TC-01: SDIO 链路通断测试

**目的：** 验证 STM32N6 ↔ ESP32-C6 SDIO 物理链路工作正常。

**步骤：**
1. 给两块板子上电
2. ESP32-C6 串口输出：`SDIO Slave initialized`
3. STM32N6 发送心跳包 (`SDIO_PKT_HEART = 0x05`)
4. ESP32-C6 接收并回复

**通过条件：**
- ☐ ESP32-C6 串口可见 `SDIO link established` 日志
- ☐ STM32N6 侧 `bsp_sdio_get_stats()` 显示 `tx_packets > 0`
- ☐ ESP32 侧 `s_stats.rx_frames > 0`
- ☐ ESP32 24小时内无 CRC 错误

**测量：**
- 心跳延迟（从发送到接收 ACK）：______ μs

---

### TC-02: VD55G1 帧捕获测试

**目的：** 验证 VD55G1 传感器初始化成功，MIPI CSI-2 → DCMIPP → SDRAM 数据通路正常。

**步骤：**
1. 烧录 STM32N6 固件并启动
2. 读取 `app_camera_get_stats()`
3. 观察 `frames_captured` 计数器是否递增

**通过条件：**
- ☐ `bsp_mipi_csi_init()` 返回 `LINGXI_OK`
- ☐ `app_camera_get_stats().frames_captured > 0`（> 5 秒后）
- ☐ `frames_dropped == 0`（无丢帧）
- ☐ `avg_process_us < 5000`（每帧处理 < 5ms）

**测量：**
- 捕获帧率：______ fps（应为 ~60fps）
- 平均处理耗时：______ μs

---

### TC-03: 下采样 + 分片编码测试

**目的：** 验证 640×480 → 320×240 下采样 + 51 分片打包的正确性。

**步骤：**
1. 确保相机工作，vTaskCamera 运行
2. 读取 `app_stream_get_stats()`
3. 检查分片计数

**通过条件：**
- ☐ `app_stream_get_stats().fragments_sent > 0`
- ☐ `last_fragment_count == 51`（77KB / 1516B ≈ 51 分片）
- ☐ `send_errors == 0`
- ☐ `frames_dropped == 0`

---

### TC-04: SDIO → Wi-Fi 转发测试

**目的：** 验证 ESP32-C6 收到图像帧分片后，通过 UDP 转发到地面站。

**步骤：**
1. 启动地面站（监听模式）：`python3 ground_station.py --no-display --record test_pre.avi`
2. 上电无人机
3. 等待 30 秒
4. 观察地面站统计输出

**通过条件：**
- ☐ 地面站输出 `Packets received > 0`
- ☐ `Frames completed > 0`
- ☐ `Frames dropped / Frames completed < 0.1`（丢帧率 < 10%）
- ☐ 录制文件 `test_pre.avi` 非空

**测量：**
- 收到分片数：______
- 完整帧数：______
- 丢帧率：______ %

---

### TC-05: 端到端延迟测量

**目的：** 测量从 VD55G1 曝光时刻到地面站显示完整帧的总延迟。

**方法：** 通过帧头时间戳 (`timestamp_us`) 与地面站接收时间戳对比。

**步骤：**
1. 启动地面站：`python3 test_latency.py`（使用专用延迟测试脚本）
2. 上电无人机
3. 采集 100 帧数据

**通过条件：**
- ☐ 平均端到端延迟 < **150ms**（Target）
- ☐ P95 延迟 < **300ms**
- ☐ 最大延迟 < **1000ms**

**测量结果：**

| 指标 | 值 | 通过标准 |
|------|-----|---------|
| 平均延迟 | ______ ms | < 150ms |
| 最小延迟 | ______ ms | — |
| P50 延迟 | ______ ms | — |
| P95 延迟 | ______ ms | < 300ms |
| 最大延迟 | ______ ms | < 1000ms |
| 标准差 | ______ ms | — |

---

### TC-06: 帧完整性验证

**目的：** 验证重组后的 RAW8 图像数据没有损坏或错位。

**方法：** 检查每帧 CRC 或内容完整性特征。

**步骤：**
1. 启动地面站（录制模式）：`python3 ground_station.py --record test_int.avi`
2. 在相机前放置棋盘格或已知图案
3. 采集 50 帧
4. 运行完整性检查：`python3 tools/ground_station/test_frame_integrity.py test_int.avi`

**通过条件：**
- ☐ 无 CRC 校验失败
- ☐ 每帧尺寸 == 320×240 (76,800 字节)
- ☐ 图像内容无明显撕裂或错位（目测）
- ☐ 平均 PSNR > 30dB（与已知图案对比）

---

### TC-07: 稳定性测试（30 分钟运行）

**目的：** 验证系统在长时间运行下的稳定性。

**步骤：**
1. 启动地面站增强模式：`python3 tools/ground_station/stability_test.py --duration 1800 --record stress.avi`
2. 上电无人机
3. 等待 30 分钟或自动结束

**通过条件：**
- ☐ 正常运行 30 分钟无崩溃
- ☐ 平均丢帧率 < 15%
- ☐ ESP32-C6 无重启（`uptime > 30min`）
- ☐ STM32N6 无 HardFault
- ☐ 地面站无 `OSError` / `socket error`

**测量：**

| 时间点 | 帧率 | 丢帧率 | ESP32 堆栈(KB) | STM32 堆栈(bytes) |
|--------|------|--------|----------------|-------------------|
| 1 min  | | | | |
| 5 min  | | | | |
| 15 min | | | | |
| 30 min | | | | |

---

### TC-08: SLAM 兼容性快速验证

**目的：** 确认地面站输出的灰度帧能被 ORB-SLAM3 消费。

**方法：** 将地面站输出管道传入 ORB-SLAM3 的 `mono_tum` 示例（或等价的轻量测试）。

**步骤：**
1. 保存 100 帧为 TUM 格式：`python3 tools/ground_station/export_tum.py --frames 100 --output ./tmp_slam/`
2. 运行 ORB-SLAM3 单目模式（如已安装）：
   ```bash
   ./Examples/Monocular/mono_tum Vocabulary/ORBvoc.txt TUM1.yaml ./tmp_slam/
   ```
3. 观察是否成功初始化

**通过条件：**
- ☐ ORB-SLAM3 成功提取 ORB 特征
- ☐ 地图初始化成功（~20 帧内）
- ☐ 跟踪不丢失（100 帧范围内）

---

## 三、自动化测试脚本

### 3.1 安装依赖

```bash
pip install opencv-python numpy psutil
```

### 3.2 使用方式

```bash
# 快速健康检查（~30 秒）
python3 tools/ground_station/test_runner.py --quick

# 完整测试套件（~35 分钟）
python3 tools/ground_station/test_runner.py --full

# 仅运行特定测试
python3 tools/ground_station/test_runner.py --test tc-05,tc-06

# 输出 JSON 报告
python3 tools/ground_station/test_runner.py --quick --report report.json
```

### 3.3 测试结果记录

测试通过率：______ / ______

**签名：** ________________

**备注：**

_________________________________________________________
_________________________________________________________
