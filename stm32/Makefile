# STM32N657 HAL+LL Makefile
# Target: STM32N657X0HxQ (AppliNonSecure)

# Toolchain
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
AR = $(PREFIX)ar
LD = $(PREFIX)gcc
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

# Target
TARGET = lingxi-bl-firmware
MCU = -mcpu=cortex-m55 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard

# Defines
DEFS = -DUSE_HAL_DRIVER
DEFS += -DUSE_FULL_LL_DRIVER
DEFS += -DSTM32N657xx
DEFS += -DCORE_CM55

# Include paths
INCLUDES = \
  -ICore/Inc \
  -IApp/inc \
  -I../shared/protocol \
  -I../shared/utils \
  -IDrivers/STM32N6xx_HAL_Driver/Inc \
  -IDrivers/CMSIS/Core/Include \
  -IDrivers/CMSIS/Device/ST/STM32N6xx/Include \
  -IMiddlewares/FreeRTOS/source/include \
  -IMiddlewares/FreeRTOS/portable/GCC/ARM_CM55_NTZ/non_secure

# C flags
CFLAGS = $(MCU) $(DEFS) $(INCLUDES)
CFLAGS += -O2 -g -Wall -fdata-sections -ffunction-sections
CFLAGS += -std=c11

# ASM flags
ASFLAGS = $(MCU) $(DEFS) $(INCLUDES) -Wall -fdata-sections -ffunction-sections

# Linker flags
LDFLAGS = $(MCU) -specs=nano.specs -TSTM32N657X0HXQ_XIP_NS.ld
LDFLAGS += -lc -lm -lnosys
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections
LDFLAGS += -Wl,--print-memory-usage

# Build directory
BUILD_DIR = build

# Source files
# HAL sources
C_SOURCES = \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cortex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma_ex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_exti.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sdram.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_gpio.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr_ex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc_ex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sd.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_sd_ex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_spi.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_spi_ex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart_ex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_usart.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_usart_ex.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_fmc.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_sdmmc.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_dma.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_gpio.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_rcc.c \
  Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_utils.c

# User Core sources (compiling)
C_SOURCES += \
  Core/Src/main.c \
  Core/Src/system_stm32n6xx_ns.c

# User drivers
C_SOURCES += \
  Core/Src/gpio_uart.c

# FreeRTOS kernel sources
C_SOURCES += \
  Middlewares/FreeRTOS/source/tasks.c \
  Middlewares/FreeRTOS/source/queue.c \
  Middlewares/FreeRTOS/source/timers.c \
  Middlewares/FreeRTOS/source/list.c \
  Middlewares/FreeRTOS/source/event_groups.c \
  Middlewares/FreeRTOS/source/stream_buffer.c \
  Middlewares/FreeRTOS/portable/MemMang/heap_4.c \
  Middlewares/FreeRTOS/portable/GCC/ARM_CM55_NTZ/non_secure/port.c \
  Middlewares/FreeRTOS/portable/GCC/ARM_CM55_NTZ/non_secure/portasm.c

# User App sources
C_SOURCES += \
  App/src/app_control.c \
  App/src/app_freertos.c \
  App/src/app_fusion.c \
  App/src/app_npu.c \
  App/src/bsp_bmp585.c \
  App/src/bsp_sdmmc2.c

# ASM sources
ASM_SOURCES = \
  Core/Startup/startup_stm32n657xx_ns.s

# Object files
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# Default target
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# Build the executable
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(LD) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

# Create hex file
$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(HEX) $< $@

# Create bin file
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf | $(BUILD_DIR)
	$(BIN) $< $@

# Compile C sources
$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

# Compile ASM sources
$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

# Create build directory
$(BUILD_DIR):
	mkdir -p $@

# Clean
clean:
	rm -rf $(BUILD_DIR)

# Flash (using openocd or stlink)
flash: $(BUILD_DIR)/$(TARGET).bin
	@echo "Flash target not configured"

# Print info
info:
	@echo "Target: $(TARGET)"
	@echo "MCU: STM32N657X0HxQ"
	@echo "Build dir: $(BUILD_DIR)"
	@echo "Sources: $(words $(C_SOURCES)) C files"
	@echo "ASM: $(words $(ASM_SOURCES)) files"

.PHONY: all clean flash info
