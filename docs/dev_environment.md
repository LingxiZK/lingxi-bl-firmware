# 灵犀智空 BL 感知模组 — 开发环境搭建指南

> **版本**: v3.2  
> **日期**: 2026-05-15  
> **适用平台**: Ubuntu 22.04/24.04 LTS (x86_64 / aarch64)

---

## 目录

1. [概述](#概述)
2. [工具链安装](#工具链安装)
3. [VSCode 配置](#vscode-配置)
4. [构建命令](#构建命令)
5. [调试配置](#调试配置)
6. [常见问题](#常见问题)

---

## 概述

本工程为跨平台固件项目，需要两套工具链：

| 目标平台 | 工具链 | 版本要求 |
|---------|--------|---------|
| STM32N657 | arm-none-eabi-gcc | 13.2+ |
| ESP32-C6 | ESP-IDF | v5.2+ |
| 调试 | OpenOCD | 0.12+ |

---

## 工具链安装

### 1. 基础依赖

```bash
sudo apt update
sudo apt install -y \
    git wget flex bison gperf python3 python3-pip python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0-dev libncurses5-dev libncursesw5-dev \
    build-essential libtool autoconf automake pkg-config
```

### 2. ARM 工具链 (STM32)

**方式一：官方预编译包（推荐）**

```bash
# 下载
wget https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz

# 解压到 /opt
sudo tar -xJf arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz -C /opt/

# 创建软链接
sudo ln -sf /opt/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi/bin/* /usr/local/bin/

# 验证
arm-none-eabi-gcc --version
```

**方式二：apt 安装（版本可能较旧）**

```bash
sudo apt install gcc-arm-none-eabi
```

### 3. ESP-IDF (ESP32-C6)

```bash
# 创建工作目录
mkdir -p ~/esp && cd ~/esp

# 克隆 ESP-IDF v5.2
git clone -b v5.2 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.2

# 安装工具链
cd esp-idf-v5.2
./install.sh esp32c6

# 设置环境变量（每次新终端需执行）
. ./export.sh
```

**添加到 `.bashrc` 便捷入口：**

```bash
echo 'alias get_idf=". $HOME/esp/esp-idf-v5.2/export.sh"' >> ~/.bashrc
source ~/.bashrc
```

### 4. OpenOCD 调试器

```bash
# 从源码编译（支持 STM32N6 最新芯片）
cd ~/esp  # 或任意工作目录
git clone https://github.com/openocd-org/openocd.git
cd openocd
./bootstrap
./configure --prefix=/usr/local
make -j$(nproc)
sudo make install

# 验证
openocd --version
```

**udev 规则（免 sudo 使用调试器）：**

```bash
# ST-Link
sudo cp /opt/arm-gnu-toolchain-*/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
# 或手动创建
sudo tee /etc/udev/rules.d/99-stlink.rules << 'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3752", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374f", MODE="0666", GROUP="plugdev"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## VSCode 配置

### 必装插件

| 插件 | ID | 用途 |
|-----|-----|-----|
| C/C++ | ms-vscode.cpptools | IntelliSense + 调试 |
| CMake Tools | ms-vscode.cmake-tools | CMake 构建 |
| Cortex-Debug | marus25.cortex-debug | ARM 调试 |
| ESP-IDF | espressif.esp-idf-extension | ESP32 开发 |
| Better C++ Syntax | jeff-hykin.better-cpp-syntax | 语法高亮 |

### 工作区配置 `.vscode/settings.json`

```json
{
    "C_Cpp.default.intelliSenseMode": "gcc-arm",
    "C_Cpp.default.compilerPath": "/usr/local/bin/arm-none-eabi-gcc",
    "C_Cpp.default.cStandard": "c11",
    "C_Cpp.default.cppStandard": "c++17",
    "cmake.configureOnOpen": false,
    "cmake.buildDirectory": "${workspaceFolder}/stm32/build",
    "cmake.generator": "Ninja",
    "cmake.toolchainFile": "${workspaceFolder}/stm32/cmake/arm-none-eabi-gcc.cmake",
    "files.associations": {
        "*.h": "c",
        "*.c": "c"
    },
    "editor.formatOnSave": true,
    "C_Cpp.formatting": "clangFormat",
    "C_Cpp.clang_format_style": "{ BasedOnStyle: LLVM, IndentWidth: 4, ColumnLimit: 100 }"
}
```

### 启动配置 `.vscode/launch.json`

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "STM32 Debug (ST-Link)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "executable": "${workspaceFolder}/stm32/build/lingxi-bl-stm32.elf",
            "configFiles": [
                "interface/stlink.cfg",
                "target/stm32n6x.cfg"
            ],
            "svdFile": "${workspaceFolder}/stm32/STM32N657.svd",
            "runToEntryPoint": "main",
            "preLaunchTask": "CMake: build"
        },
        {
            "name": "STM32 Debug (J-Link)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "jlink",
            "executable": "${workspaceFolder}/stm32/build/lingxi-bl-stm32.elf",
            "device": "STM32N657L0H3Q",
            "interface": "swd",
            "runToEntryPoint": "main",
            "preLaunchTask": "CMake: build"
        },
        {
            "name": "ESP32-C6 Debug",
            "type": "espidf",
            "request": "launch",
            "verifyBinaries": true
        }
    ]
}
```

### 任务配置 `.vscode/tasks.json`

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build STM32",
            "type": "shell",
            "command": "cmake",
            "args": [
                "--build", "${workspaceFolder}/stm32/build"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "Build ESP32",
            "type": "shell",
            "command": "idf.py",
            "args": [
                "-C", "${workspaceFolder}/esp32",
                "build"
            ],
            "group": "build",
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "Flash STM32",
            "type": "shell",
            "command": "openocd",
            "args": [
                "-f", "interface/stlink.cfg",
                "-f", "target/stm32n6x.cfg",
                "-c", "program ${workspaceFolder}/stm32/build/lingxi-bl-stm32.bin 0x08000000 verify reset exit"
            ],
            "group": "build"
        },
        {
            "label": "Flash ESP32",
            "type": "shell",
            "command": "idf.py",
            "args": [
                "-C", "${workspaceFolder}/esp32",
                "flash"
            ],
            "group": "build"
        }
    ]
}
```

---

## 构建命令

### STM32 构建

```bash
# 首次配置
cd stm32
cmake -B build \
    -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build

# 生成 hex / bin
arm-none-eabi-objcopy -O ihex build/lingxi-bl-stm32.elf build/lingxi-bl-stm32.hex
arm-none-eabi-objcopy -O binary build/lingxi-bl-stm32.elf build/lingxi-bl-stm32.bin

# 打印尺寸
arm-none-eabi-size build/lingxi-bl-stm32.elf
```

### ESP32-C6 构建

```bash
# 加载环境
get_idf

cd esp32

# 配置（首次或修改 sdkconfig 后）
idf.py set-target esp32c6
idf.py menuconfig

# 编译
idf.py build

# 烧录
idf.py -p /dev/ttyUSB0 flash

# 监视串口
idf.py -p /dev/ttyUSB0 monitor

# 编译+烧录+监视
idf.py -p /dev/ttyUSB0 flash monitor
```

### 清理构建

```bash
# STM32
rm -rf stm32/build

# ESP32
idf.py fullclean
```

---

## 调试配置

### SWD 调试 (ST-Link)

```bash
# 启动 OpenOCD 服务器
openocd -f interface/stlink.cfg -f target/stm32n6x.cfg

# 另开终端，连接 GDB
arm-none-eabi-gdb build/lingxi-bl-stm32.elf
(gdb) target remote localhost:3333
(gdb) monitor reset halt
(gdb) load
(gdb) continue
```

### JTAG 调试 (J-Link)

```bash
# J-Link GDB Server
JLinkGDBServer -device STM32N657L0H3Q -if SWD -speed 4000

# GDB 连接
arm-none-eabi-gdb build/lingxi-bl-stm32.elf
(gdb) target remote localhost:2331
```

### 串口日志

| 设备 | 波特率 | 端口 | 说明 |
|-----|--------|------|------|
| STM32 | 115200 | `/dev/ttyACM0` | 调试串口 (USART1) |
| ESP32 | 115200 | `/dev/ttyUSB0` | 默认 UART0 |

```bash
# 查看串口设备
ls -la /dev/tty*

# 串口监视（安装 picocom）
sudo apt install picocom
picocom -b 115200 /dev/ttyACM0

# 或使用 minicom
sudo apt install minicom
minicom -D /dev/ttyACM0 -b 115200
```

### 日志输出到文件

```bash
# 同时显示和保存
picocom -b 115200 /dev/ttyACM0 | tee stm32_log_$(date +%Y%m%d_%H%M%S).txt
```

---

## 常见问题

### Q1: `arm-none-eabi-gcc: command not found`

```bash
# 检查 PATH
echo $PATH | grep arm

# 手动添加
export PATH=$PATH:/opt/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi/bin
```

### Q2: ESP-IDF 安装失败

```bash
# 重新安装
cd ~/esp/esp-idf-v5.2
./install.sh --esp32c6

# 如网络问题，使用国内镜像
export IDF_GITHUB_ASSETS="dl.espressif.com/github_assets"
./install.sh esp32c6
```

### Q3: OpenOCD 无法识别 ST-Link

```bash
# 检查设备
lsusb | grep ST

# 检查权限
groups  # 确认用户在 plugdev 组

# 重新加载 udev
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### Q4: CMake 找不到编译器

```bash
# 显式指定工具链
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/arm-none-eabi-gcc.cmake

# 检查 toolchain 文件内容
cat cmake/arm-none-eabi-gcc.cmake
```

### Q5: 多平台同时开发

建议使用 VSCode 多根工作区：

```json
// lingxi-bl-firmware.code-workspace
{
    "folders": [
        { "path": "stm32", "name": "STM32" },
        { "path": "esp32", "name": "ESP32-C6" },
        { "path": "shared", "name": "Shared" },
        { "path": "tests", "name": "Tests" }
    ],
    "settings": {
        "files.exclude": {
            "**/build/**": true
        }
    }
}
```

---

## 参考资源

- [ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
- [ESP-IDF 文档](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c6/)
- [OpenOCD 文档](https://openocd.org/doc/html/)
- [STM32N6 参考手册](https://www.st.com/en/microcontrollers-microprocessors/stm32n6-series.html)

---

*文档结束*
