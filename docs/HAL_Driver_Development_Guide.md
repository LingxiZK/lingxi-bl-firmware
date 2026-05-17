# 灵犀智空 Lingxi BL — HAL驱动开发指南

**日期**: 2026-05-15  
**版本**: v3.2  
**目标**: 在VSCode工程框架下完成所有HAL驱动开发

---

## 1. 开发环境

### 1.1 工具链
- **编译器**: arm-none-eabi-gcc (>= 10.3)
- **IDE**: VSCode + CMake Tools + C/C++插件
- **调试器**: OpenOCD + ST-Link V3
- **配置工具**: STM32CubeMX 6.10+

### 1.2 工程路径
```bash
cd /home/guotong/.openclaw/workspace/lingxi-bl-firmware/stm32/
```

### 1.3 构建命令
```bash
# 配置
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake -G Ninja

# 编译
cmake --build build --parallel

# 烧录
openocd -f interface/stlink.cfg -f target/stm32n6x.cfg \
  -c "program build/LingxiBL_STM32.elf verify reset exit"
```

---

## 2. 外设驱动开发清单

### 优先级P0（本周完成）

| # | 外设 | 功能 | 复杂度 | 依赖 |
|---|------|------|--------|------|
| 1 | **RCC/时钟** | 配置800MHz系统时钟 | 高 | 无 |
| 2 | **GPIO/UART** | 调试串口（PA9/PA10） | 低 | 无 |
| 3 | **SDMMC1** | SDIO Host与ESP32通信 | 中 | RCC, GPIO |
| 4 | **FMC** | SDRAM初始化（IS42S32160F） | 高 | RCC, GPIO |

### 优先级P1（下周完成）

| # | 外设 | 功能 | 复杂度 | 依赖 |
|---|------|------|--------|------|
| 5 | **MIPI CSI-2** | VD55G1图像采集 | 高 | RCC, DMA |
| 6 | **XSPI1** | OctoSPI Flash读写 | 中 | RCC, GPIO |
| 7 | **SPI1** | DWM3000 UWB通信 | 低 | RCC, GPIO |
| 8 | **I2C1** | VL53L1X ToF通信 | 低 | RCC, GPIO |

### 优先级P2（第三周）

| # | 外设 | 功能 | 复杂度 | 依赖 |
|---|------|------|--------|------|
| 9 | **SPI2** | microSD SPI模式 | 低 | RCC, GPIO |
| 10 | **DMA/MDMA** | 高速数据传输 | 中 | RCC |
| 11 | **GPIO/中断** | UWB_IRQ, ToF_INT, SYNC_OUT | 低 | GPIO |
| 12 | **PWR/低功耗** | 睡眠/唤醒管理 | 中 | 所有外设 |

---

## 3. 各外设详细配置

### 3.1 RCC时钟配置（P0）

**目标**: 800MHz系统时钟

```c
/* 时钟树配置 */
SYSCLK  = 800 MHz  (PLL1)
HCLK    = 400 MHz  (SYSCLK / 2)
PCLK1   = 200 MHz  (HCLK / 2)
PCLK2   = 200 MHz  (HCLK / 2)
FMC_CLK = 166 MHz  (来自PLL2或PLL3)
MIPI_CLK = 250 MHz (来自PLL3)
```

**注意**: 
- STM32N657L0H3Q支持800MHz，需确认电压和散热
- FMC时钟166MHz需精确匹配SDRAM
- MIPI时钟250MHz需满足VD55G1要求

### 3.2 SDMMC1配置（P0）

**模式**: SDIO Host, 4-bit
**引脚**:
- CLK: PC12 (AF12)
- CMD: PD2 (AF12)
- D0-D3: PC8-PC11 (AF12)
**时钟**: 25-50MHz
**DMA**: MDMA_Channel0

**关键代码**:
```c
sdmmc.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
sdmmc.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
sdmmc.Init.BusWide = SDMMC_BUS_WIDE_4B;
sdmmc.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
sdmmc.Init.ClockDiv = 2;  /* 200MHz / (2+2) = 50MHz */
```

### 3.3 FMC SDRAM配置（P0）

**器件**: IS42S32160F, 32-bit, 64MB, 166MHz
**引脚**:
- D0-D15: PD, PE (AF12)
- D16-D31: PF, PG (AF12)
- A0-A12: PF, PG (AF12)
- BA0-BA1: PG4, PG5 (AF12)
- SDCLK: PG8 (AF12)
- SDCKE0: PH2 (AF12)
- SDNE0: PH3 (AF12)
- NBL0-NBL3: PE0, PE1, PG9, PG10 (AF12)
- NWE: PD5 (AF12)

**时序参数** (166MHz, CL=3):
```c
/* 关键时序 (以时钟周期为单位) */
T_RP  = 3;  /* Row Precharge: 18ns / 6ns = 3 */
T_RCD = 3;  /* RAS to CAS: 18ns / 6ns = 3 */
T_WR  = 2;  /* Write Recovery: 12ns / 6ns = 2 */
T_RAS = 6;  /* Row Active: 42ns / 6ns = 7 → 取7 */
T_RC  = 9;  /* Row Cycle: 60ns / 6ns = 10 → 取10 */
T_XSR = 10; /* Exit Self Refresh */
T_MRD = 2;  /* Mode Register */
```

### 3.4 MIPI CSI-2配置（P1）

**模式**: 单Lane, 640×480@60fps
**引脚**:
- D0P/D0N: PH4/PH5 (AF13)
- CLKP/CLKN: PH6/PH7 (AF13)
**数据率**: 500Mbps
**DMA**: MDMA_Channel1 (双缓冲)

**注意**:
- 需配置MIPI D-PHY
- 图像格式: YUV422 (每像素2字节)
- 每帧: 640×480×2 = 614KB
- 帧缓冲: 双缓冲，放在SDRAM

### 3.5 XSPI1 OctoSPI配置（P1）

**器件**: W25Q128, 16MB, 8线
**引脚**:
- CLK: PB2 (AF10)
- D0-D7: PB0, PB1, PC13, PC2, PC3, PC4, PD11, PD12 (AF10)
- NCS: PB6 (AF10)
**时钟**: 100-133MHz
**模式**: OctoSPI DTR

### 3.6 SPI1配置（P1）

**器件**: DWM3000
**引脚**:
- SCK: PA5 (AF5)
- MISO: PA6 (AF5)
- MOSI: PA7 (AF5)
- NSS: PA4 (软件控制)
- IRQ: PA8 (外部中断)
- RST: PA11 (GPIO)
- SYNC: PA12 (GPIO)
**时钟**: 10MHz
**模式**: CPOL=0, CPHA=0

### 3.7 I2C1配置（P1）

**器件**: VL53L1X
**引脚**:
- SCL: PB8 (AF4)
- SDA: PB9 (AF4)
- XSHUT: PB10 (GPIO)
- GPIO1: PB11 (外部中断)
**时钟**: 400kHz (Fast Mode)

### 3.8 SPI2配置（P2）

**器件**: microSD (SPI模式)
**引脚**:
- SCK: PB13 (AF5)
- MISO: PB14 (AF5)
- MOSI: PB15 (AF5)
- CS: PB12 (GPIO)
- CD: PD3 (GPIO输入)
**时钟**: 25MHz

---

## 4. 开发顺序建议

### Week 1: 基础驱动
1. **Day 1-2**: RCC时钟 + GPIO + UART调试
2. **Day 3-4**: SDMMC1 (与ESP32通信验证)
3. **Day 5**: FMC SDRAM (内存测试)

### Week 2: 核心外设
4. **Day 1-3**: MIPI CSI-2 (图像采集验证)
5. **Day 4**: XSPI1 Flash (读写测试)
6. **Day 5**: SPI1 + I2C1 (传感器通信)

### Week 3: 完善
7. **Day 1-2**: SPI2 microSD + GPIO中断
8. **Day 3-4**: DMA/MDMA优化
9. **Day 5**: 低功耗 + 系统整合

---

## 5. 调试策略

### 5.1 分阶段验证

| 阶段 | 验证内容 | 通过标准 |
|------|---------|---------|
| 1 | UART打印 + LED闪烁 | 串口输出正常 |
| 2 | SDMMC ↔ ESP32 Ping | ESP32响应心跳 |
| 3 | SDRAM读写测试 | 64MB全部通过 |
| 4 | MIPI图像采集 | 640×480 YUV输出 |
| 5 | XSPI Flash读写 | 16MB读写正确 |
| 6 | 传感器读取 | ToF距离 + UWB坐标 |
| 7 | 全系统联调 | 5个任务同时运行 |

### 5.2 调试工具
- **OpenOCD + GDB**: 断点调试
- **逻辑分析仪**: MIPI/SDIO/SPI时序
- **串口助手**: 协议帧查看
- **示波器**: 电源纹波、时钟质量

---

## 6. 代码规范

### 6.1 命名规范
```
文件: lx_<module>_<device>.c/h
函数: LX_<Module>_<Action>()
变量: s_<name> (静态), g_<name> (全局)
常量: LX_<NAME>_MAX/MIN/DEF
```

### 6.2 错误处理
```c
typedef enum {
    LX_OK = 0,
    LX_ERR_TIMEOUT = -1,
    LX_ERR_BUSY = -2,
    LX_ERR_INVALID = -3,
    LX_ERR_IO = -4,
    LX_ERR_MEMORY = -5,
} lx_err_t;
```

### 6.3 注释规范
- 函数头注释（Doxygen格式）
- 关键时序参数注释
- 魔法数字必须说明

---

## 7. 交付标准

- [ ] 所有P0外设驱动可编译通过
- [ ] 所有P1外设驱动通过单元测试
- [ ] 全系统联调通过（5个任务同时运行）
- [ ] 功耗测试通过（典型<2.65W，全速<3.2W）
- [ ] 代码审查通过（cppcheck + MISRA C:2012）
