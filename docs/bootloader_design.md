# Lingxi N6 AI Deck — Bootloader 设计方案

> 版本: v1.0 | 日期: 2026-05-16 | 适配: STM32N657X0 + W25Q512 OctoSPI

---

## 1. 设计目标

1. **无需接触物理接口**—通过无人机 UART 线完成 Deck 固件升级。
2. **掉电安全**—A/B 双分区，任意时刻掉电不会破坏启动能力。
3. **与现有协议兼容**—OTA 命令码与 `lingxi_protocol.h` 中 `LX_CMD_OTA_*` 保持一致。
4. **可回滚**—升级失败或新固件异常时，自动/手动回滚到旧版本。

---

## 2. 系统架构

```
┌───────────────────────────────────────────────────────────────────────────┐
│                        无人机主控 (Crazyflie / 自研飞控)              │
│                                 UART TX/RX                                │
└───────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌───────────────────────────────────────────────────────────────────────────┐
│                        Lingxi N6 AI Deck (STM32N657)                         │
│  ┌────────────────────┐  ┌────────────────────┐                        │
│  │   Internal Flash    │  │    OctoSPI XIP      │                        │
│  │  0x0800_0000        │  │  0x7018_0000        │                        │
│  │  ├── Bootloader     │  │  ├── App A (1MB)   │  ← 激活分区         │
│  │  ├── Info Sector    │  │  ├── App B (1MB)   │  ← 备份分区         │
│  │  └── OTA Cache      │  │  └── App Info       │                        │
│  └────────────────────┘  └────────────────────┘                        │
│                                                                         │
│  UART1 (PA9/PA10) ←──────────────────────────────────────────────→ SDIO → ESP32-C6    │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Flash 分区详解

| 区域 | 地址 | 大小 | 用途 |
|-------|-------|------|------|
| Bootloader | 0x0800_0000 | 64KB | 启动代码，永不被覆盖 |
| Info Sector| 0x0801_0000 | 8KB  | 分区标志、版本、CRC、boot count |
| OTA Cache  | 0x0801_2000 | ~1.9MB | 接收缓存（可选） |
| App A      | 0x7018_0000 | 1MB  | 当前激活或下一个激活的固件 |
| App B      | 0x7028_0000 | 1MB  | 备份分区 |
| App Info   | 0x7038_0000 | 16KB | XIP Info Sector（可选） |

**A/B 切换原理**: Info Sector 中 `active_partition` 字段决定启动哪个分区。升级时总是写入非激活分区，提交时仅更新 `active_partition`。

---

## 4. Bootloader 状态机

```
┌─────────────────────────────────────────────────────────────────────┐
│                              上电 / 复位                               │
│                                   ▼                                      │
│                          ┌─────────────┐                                │
│                          │  初始化硬件   │                                │
│                          │  (RCC/UART/XSPI)│                                │
│                          └─────┼─────┘                                │
│                                   ▼                                      │
│                          ┌─────────────┐                                │
│                          │  读取 Info     │                                │
│                          │  Sector        │                                │
│                          └─────┼─────┘                                │
│                                   ▼                                      │
│            ┌───────────────┼───────────────┐                                │
│            │ Info 无效   │   │ Info 有效    │                                │
│            │ (init default)│   │             │                                │
│            └────────────┼──┘                                │
│                         │   ▼                                      │
│                         │ ┌─────────────┐                                │
│                         │ │ 3s 超时监听   │                                │
│                         │ │ UART 命令   │                                │
│                         │ └─────┼─────┘                                │
│            ┌───────────────┼───────────────┐                                │
│            │ 超时 / 无命令│   │ ENTER_BOOT   │                                │
│            │             │   │ (0x01)      │                                │
│            └────────────┼──┘                                │
│                         │   ▼                                      │
│                         │ ┌─────────────┐                                │
│                         │ │  升级模式    │                                │
│                         │ │  (OTA状态机)│                                │
│                         │ └─────────────┘                                │
│                         │   │                                      │
│            ┌───────────────┼───────────────┐                                │
│            │ 校验失败   │   │ COMMIT 成功  │                                │
│            │ (尝试fallback)│   │             │                                │
│            └────────────┼──┘                                │
│                         │   ▼                                      │
│                         │ ┌─────────────┐                                │
│                         │ │ 跳转到 XIP   │                                │
│                         │ │ App (A/B)   │                                │
│                         │ └─────────────┘                                │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 5. UART OTA 协议详解

### 5.1 帧格式

```
+------+--------+-----+-----+---------+--------+
| SOF  | LEN    | CMD | SEQ | PAYLOAD | CRC16  |
| 0xAA | 2 LE   | 1   | 1   | N       | 2 LE   |
+------+--------+-----+-----+---------+--------+
```

- **SOF**: 固定 `0xAA`
- **LEN**: payload 长度（不含 status，ACK 时 payload 包含 1 字节 status + 数据）
- **CMD**: 命令码，与 `lingxi_protocol.h` 中 `LX_CMD_OTA_*` 对齐
- **SEQ**: 序列号，用于匹配 ACK
- **CRC16**: Modbus CRC16，计算范围为 SOF 到 PAYLOAD 末尾

### 5.2 升级流程时序

```
无人机                          Deck (Bootloader)
  |                                    |
  |--- OTA_START (size, crc, ver) --->|
  |<-- ACK (status, max_chunk, addr) --|
  |                                    |
  |--- OTA_CHUNK (offset, data) ----->|
  |<-- ACK (status, next_offset) ------|
  |   (重复直到所有数据发送完毕)        |
  |                                    |
  |--- OTA_VERIFY ------------------->|
  |<-- ACK (status, calc_crc32) -------|
  |                                    |
  |--- OTA_COMMIT ------------------->|
  |<-- ACK (status=OK) ----------------|
  |                                    |
  |      [Deck 自动复位并启动新固件]      |
```

### 5.3 响应状态码

| 码 | 含义 |
|-----|------|
| 0x00 | OK |
| 0x01 | CRC 错误 |
| 0x02 | 长度/大小错误 |
| 0x03 | 命令不支持 |
| 0x04 | Flash 操作失败 |
| 0x05 | 正忙（OTA 进行中） |
| 0x06 | 校验失败 |

---

## 6. 与现有项目的集成

### 6.1 与 `lingxi_protocol` 的关系

| lingxi_protocol (STM32↔ESP32) | Bootloader UART 协议 |
|--------------------------------|---------------------|
| `LX_CMD_OTA_START (0x60)`      | `BL_CMD_OTA_START (0x60)` |
| `LX_CMD_OTA_CHUNK (0x61)`      | `BL_CMD_OTA_CHUNK (0x62)` |
| `LX_CMD_OTA_VERIFY (0x62)`     | `BL_CMD_OTA_VERIFY (0x64)` |
| `LX_CMD_OTA_COMMIT (0x63)`     | `BL_CMD_OTA_COMMIT (0x66)` |
| `LX_CMD_OTA_ROLLBACK (0x64)`   | `BL_CMD_OTA_ROLLBACK (0x68)` |

> 说明: Bootloader 命令码保持同步但隔一个编号，避免混淆。上位机可以共用同一套命令定义。

### 6.2 与 ESP32 OTA 的协同

若升级目标是 ESP32（通过 SDIO）：
1. Bootloader 收到 `OTA_START` 时检查 `target_chip`
2. 若为 ESP32，通过 SDIO 发送 `LX_CMD_OTA_START` 给 ESP32
3. 后续 `OTA_CHUNK` 数据通过 SDIO 透传到 ESP32
4. ESP32 使用现有 `ota_manager` 组件完成写入和校验
5. Bootloader 最终发送 `REBOOT` 给 ESP32

### 6.3 与无人机的接口

推荐在无人机固件中增加一个 **Deck OTA 模块**：

```c
// 无人机端伪代码
bool deck_ota_start(uint32_t size, uint32_t crc, const char *ver);
bool deck_ota_send_chunk(uint32_t offset, const uint8_t *data, uint16_t len);
bool deck_ota_verify(void);
bool deck_ota_commit(void);
```

对于 Crazyflie，可以利用现有 CRTP 协议隧道传输升级数据，或直接通过 UART 物理层发送。

---

## 7. 安全机制

1. **双备份 (A/B)**: 无论何时掉电，总有一个可用分区。
2. **原子更新**: Info Sector 先写到 Internal Flash，再设置标志，避免写一半时掉电。
3. **校验先于提交**: `OTA_COMMIT` 时再次校验 CRC，确保数据完整性。
4. **回滚**: 新固件启动失败（看门狗复位超过阈值）时，bootloader 可自动切换回旧分区。
5. **MSP 验证**: 跳转前检查 App 的 MSP 是否在有效 RAM 范围。
6. **看门狗**: 升级过程 10s 超时自动复位，防止死循环。

---

## 8. 后续工作

- [ ] 配置 CubeMX 生成 Bootloader 工程的 HAL 初始化代码
- [ ] 编写 `bl_linker.ld`使 Bootloader 链接到 0x0800_0000 (64KB)
- [ ] 确认 STM32N6 的 Flash sector 大小（N6 可能为 8KB/sector）
- [ ] 调试 XSPI HAL 并确认 W25Q512 的 4-byte address 模式
- [ ] 在无人机端实现 OTA 上位机（Python 或 C）
- [ ] 添加固件签名验证（ECC/RSA 或 HMAC，可选）
