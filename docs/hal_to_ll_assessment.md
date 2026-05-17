# STM32N6 工程 HAL → LL 库替换技术评估报告

> 版本: v1.0 | 日期: 2026-05-16 | 适配: STM32N657X0 | 作者: Lingxi FW Team

---

## 1. 当前 HAL 使用深度统计

### 1.1 Bootloader (stm32/Bootloader/)

| 模块 | HAL 调用数 | 核心函数 | 复杂度 |
|------|-----------|---------|--------|
| XSPI | ~35 | `HAL_XSPI_Init`, `HAL_XSPI_Command`, `HAL_XSPI_Receive/Transmit` | 高 |
| Flash | ~5 | `HAL_FLASHEx_Erase`, `HAL_FLASH_Program` | 中 |
| UART | ~6 | `HAL_UART_Init`, `HAL_UART_Transmit`, `HAL_UART_Receive_IT` | 中 |
| RCC | ~5 | `HAL_RCC_OscConfig`, `HAL_RCC_ClockConfig`, `HAL_RCC_DeInit` | 高 |
| GPIO | ~3 | `HAL_GPIO_Init`, `HAL_GPIO_TogglePin` | 低 |
| NVIC | ~2 | `HAL_NVIC_SetPriority`, `HAL_NVIC_EnableIRQ` | 低 |
| SysTick | ~2 | `HAL_SYSTICK_Config`, `HAL_SYSTICK_CLKSourceConfig` | 低 |
| IWDG | ~2 | `HAL_IWDG_Init`, `HAL_IWDG_Refresh` | 低 |
| 通用 | ~3 | `HAL_Init`, `HAL_DeInit`, `HAL_GetTick`, `HAL_Delay` | 低 |
| **合计** | **~63** | | |

### 1.2 Core/App (stm32/Core/ + stm32/App/)

| 模块 | HAL 调用数 | 复杂度 |
|------|-----------|--------|
| MDMA | ~80+ | 高 |
| SDRAM | ~40+ | 高 |
| XSPI Flash | ~60+ | 高 |
| SPI (UWB/BMP585) | ~40+ | 中 |
| I2C (ToF/Camera) | ~30+ | 中 |
| UART (Debug/FC) | ~20+ | 中 |
| GPIO | ~100+ | 低 |
| **合计** | **~370+** | |

> **结论**: 当前工程对 HAL 依赖极深，Core/App 层有 370+ 处 HAL 调用，完全替换为 LL 工作量巨大。

---

## 2. HAL vs LL 核心差异

| 维度 | HAL | LL | 影响 |
|------|-----|----|------|
| **抽象层级** | 高层 API，状态机驱动 | 直接寄存器操作 | HAL 更安全，LL 更高效 |
| **代码 Footprint** | 大（状态机+回调+错误检查） | 小（内联宏/函数） | LL 约减少 30~60% ROM |
| **执行时间** | 有函数调用开销 | 几乎零开销 | LL 约快 2~5x |
| **错误检查** | 参数验证、状态检查、时序保护 | 无保护，直接操作寄存器 | LL 更容易 HardFault |
| **中断管理** | 完整回调机制 | 需手动管理 | LL 需自己写 ISR |
| **可移植性** | 跨系列兼容 | 系列特异 | LL 代码不能直接移植 |
| **勋误表处理** | HAL 已包含已知勋误 | 需手动处理 | N6 新系列勋误较多 |
| **调试友好性** | 状态可跟踪 | 难跟踪 | HAL 更适合调试 |

---

## 3. 逐模块替换可行性分析

### 3.1 ⚠️ 不推荐替换（风险 > 收益）

#### XSPI (OctoSPI Flash 控制器)
- **复杂度**: 极高
- **N6 特殊性**: XSPIM 是 N6 新增的复杂外设，包含：
  - 2 个独立 XSPI 控制器 (XSPI1/XSPI2)
  - 最高 8 线 STR / 4 线 DTR 模式
  - 自动刷新、代码执行、XiP 模式
  - 寄存器配置超过 20 个字段（CR、DCR1/2/3、CCR、WCCR、IR、ARR、ABR、DR、PSMAR、PSMKR、PIR、TCR　…）
- **HAL作用**: `HAL_XSPI_Init()` 已封装了所有这些寄存器的正确配置顺序，包括：
  - 先配置 CRL、CR2，再等待 BUSY 清零
  - 等待 FIFO 清空
  - 启动 XSPIM 映射
- **LL替换风险**: 手动配置顺序错误可能导致 XSPI 卡死，XiP 模式无法恢复
- **建议**: 严禁替换，保留 HAL_XSPI

#### Flash 操作 (Internal Flash 编程)
- **复杂度**: 高
- **N6 特殊性**: 
  - 128-bit 编程宽度 (Quadword)
  - 需要 16-byte 对齐
  - Flash 编程有特殊时序（先擦除再编程，编程前需检查忙碌位）
  - 寄存器: FLASH_ACR, FLASH_KEYR1, FLASH_CR1, FLASH_SR1, FLASH_CCR1　…
- **HAL作用**: `HAL_FLASH_Program()` 处理了所有时序和状态检查，包括顺序大小端转换、等待 BSY 清零
- **LL替换风险**: N6 是新系列，Flash 编程流程可能与旧系列不同，手动实现容易出现势误表兼容问题
- **建议**: 严禁替换，保留 HAL_FLASH

#### RCC 时钟配置
- **复杂度**: 极高
- **N6 特殊性**:
  - 3 个独立 PLL (PLL1/PLL2/PLL3)
  - 每个 PLL 有 VCO + 3 级分频 (P/Q/R)
  - 时钟树: CPUCLK, SYSCLK, HCLK, AXI, AHB, APB1~APB5, 外设时钟
  - Flash latency 与供电电压联动
- **HAL作用**: `HAL_RCC_OscConfig()` 自动计算所有分频器，验证兼容性
- **LL替换风险**: 手动计算 PLL 分频系数极易出错，N6 时钟树复杂程度远超 F4/G4
- **建议**: 严禁替换，保留 HAL_RCC

### 3.2 ✅ 可替换（收益 > 风险）

#### GPIO 操作
- **复杂度**: 极低
- **LL API**: `LL_GPIO_SetPinMode()`, `LL_GPIO_SetPinSpeed()`, `LL_GPIO_SetAFPin_8_15()`, `LL_GPIO_SetOutputPin()`
- **Bootloader 中的 HAL_GPIO_Init**: 只用于 PA5 LED、UART GPIO、SWD GPIO
- **代码量**: 可减少约 500B
- **建议**: 推荐替换

#### IWDG (独立看门狗)
- **复杂度**: 极低
- **LL API**: `LL_IWDG_Enable()`, `LL_IWDG_ReloadCounter()`, `LL_IWDG_SetPrescaler()`
- **Bootloader 中仅 2 处调用**: Init + Refresh
- **建议**: 推荐替换

#### SysTick
- **复杂度**: 低
- **LL API**: `LL_SYSTICK_SetClkSource()`, `直接操作 SysTick->LOAD/VAL/CTRL`
- **建议**: 可替换，但收益微乎其微

### 3.3 ⚠️ 可替换但需谨慎（中等风险）

#### UART 操作
- **发送**: LL 替换简单，`LL_USART_TransmitData8()` + 等待 TXE
- **中断接收**: 需要手动管理 NVIC 和 USART_SR 位，较麻烦
- **建议**: 发送可替换，中断接收建议保留 HAL

#### NVIC 中断管理
- **LL API**: `直接操作 NVIC->ISER[x]`, `NVIC->IP[x]`
- **复杂度**: 低，但需注意优先级分组计算
- **建议**: 可替换，但收益不大

---

## 4. 稳定性评估

### 4.1 LL 库本身的可靠性

| 项目 | 评估 | 说明 |
|------|-------|------|
| ST 官方维护 | ✓ 可靠 | LL 与 HAL 同步发布，经过相同测试流程 |
| 寄存器映射正确性 | ✓ 可靠 | LL 宏直接操作寄存器，没有浪费指令 |
| 中断安全性 | ⚠️ 中等 | LL 不做中断封装，开发者必须自己管理 |
| 时序保护 | ✗ 缺失 | LL 不检查寄存器状态位，错误操作可能导致硬件卡死 |

### 4.2 STM32N6 特有风险

| 风险项 | 严重级 | 说明 |
|--------|---------|------|
| 勋误表兼容性 | 高 | N6 是新系列，部分外设寄存器与旧系列不同，HAL 已处理 |
| Flash 编程时序 | 高 | N6 Flash 使用 Quadword 编程，时序误差导致 HardFault |
| XSPIM 配置 | 极高 | XiP 配置错误可能导致代码无法运行，J-Link 也无法调试 |
| 时钟配置 | 中 | PLL 配置错误可能导致 MCU 无法启动，需重新烧录 Bootloader |

### 4.3 混合使用风险

- **HAL + LL 混用本身是安全的**，ST 官方允许并鼓励混合使用
- 但需避免：同一外设同时用 HAL 和 LL 操作，导致 HAL 状态机与实际寄存器状态不一致
- 实践中：可以 HAL 初始化 + LL 操作，但不要在 HAL“操作中”插入 LL 修改寄存器

---

## 5. 代码量收益估算

### 5.1 Bootloader (64KB 链接空间)

| 方案 | 预估代码量 | 占用比例 | 剩余空间 |
|------|-----------|---------|---------|
| 全 HAL | ~45KB | 70% | 19KB |
| HAL + LL GPIO/IWDG | ~43KB | 67% | 21KB |
| 全 LL | ~30KB | 47% | 34KB |

> **结论**: 即使全 HAL，64KB 也足够容纳 Bootloader，代码量压缩不是紧急需求。

### 5.2 App (1MB XIP 链接空间)

| 方案 | 预估代码量 | 占用比例 | 剩余空间 |
|------|-----------|---------|---------|
| 全 HAL | ~200KB | 20% | 800KB |
| 全 LL | ~140KB | 14% | 860KB |

> **结论**: App 有充足的 1MB 空间，代码量不是瓶颈。

---

## 6. 最终建议

### 6.1 对于 Bootloader

**推荐策略: 保留 HAL，局部优化**

```
保留 HAL:
  ✓ HAL_XSPI_*   (XSPI 配置极其复杂，勋误表风险高)
  ✓ HAL_FLASH_*  (Flash 编程时序严格，N6 新特性)
  ✓ HAL_RCC_*    (RCC 配置复杂，自动验证)
  ✓ HAL_UART_*   (中断接收的回调机制)
  ✓ HAL_NVIC_*   (中断优先级分组计算)

可以替换为 LL (如果愿意):
  ○ LL_GPIO_*    (简单安全，可减少 ~500B)
  ○ LL_IWDG_*    (极简单，可减少 ~200B)
  ○ 直接操作 SysTick 寄存器 (微乎其微)
```

**原因**:
1. Bootloader 只有 64KB，当前全 HAL 预估也只占 ~45KB，空间充足
2. Bootloader 的核心功能是 OTA 升级，任何稳定性风险都不值得
3. XSPI + Flash 是 Bootloader 的命脉，这两个 HAL 封装是必需的

### 6.2 对于 Core/App

**推荐策略: 保留 HAL**

原因:
1. Core/App 有 370+ 处 HAL 调用，完全替换工作量巨大（估计 2~3 人月）
2. App 有 1MB XIP 空间，代码量不是瓶颈
3. MDMA、SDRAM、MIPI CSI-2 等外设的 HAL 封装极其复杂，替换风险极高
4. 时间收益比不合适——HAL 的性能已经足够满足无人机应用场景

### 6.3 唯一建议改用 LL 的场景

如果未来遇到以下情况，可以考虑局部使用 LL：
- 某个 ISR 需要极低延迟（如高频 PWM 输出）
- 某个低层驱动需要精确控制执行时序（如软件 SPI/I2C 模拟）
- Flash footprint 确实不够用（但当前远未触及这个界限）

---

## 7. 如果坚持替换的实施路线

如果团队决定全面向 LL 迁移，建议分阶段进行：

### 阶段 1: 证明概念 (PoC)
- 目标: 只替换 GPIO + IWDG，验证构建和运行
- 工作量: ~1 人天
- 风险: 极低

### 阶段 2: UART LL 化
- 目标: 将 UART 发送/中断接收改为 LL
- 工作量: ~3 人天
- 风险: 中等

### 阶段 3: Flash LL 化
- 目标: 用 LL 实现 Flash 擦除和编程
- 工作量: ~5 人天
- 风险: 高 (需要对照 N6 参考手册每个寄存器位)

### 阶段 4: XSPI LL 化 (不推荐)
- 目标: 用 LL 实现 XSPI 初始化和操作
- 工作量: ~10 人天
- 风险: 极高 (不推荐)

---

## 8. 总结

| 维度 | 评估结论 |
|------|----------|
| **技术可行性** | 部分可行 (GPIO/IWDG/UART 发送)，部分不可行 (XSPI/Flash/RCC) |
| **工作量** | 全替换需 2~3 人月，部分替换需 ~1 周 |
| **代码量收益** | ~15~30% ROM 减少，但当前并非瓶颈 |
| **性能收益** | ~2~5x 某些操作，但无人机场景不敏感 |
| **稳定性风险** | 中等：LL 缺少参数验证和时序保护，N6 新系列勋误表兼容性未充分验证 |
| **维护成本** | 替换后代码难以调试，新团队成员学习曲线更陡 |
| **最终建议** | **保留 HAL，局部优化**：如果确实需要减少代码量，只替换 GPIO/IWDG |
