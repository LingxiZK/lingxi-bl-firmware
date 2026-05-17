cmake_minimum_required(VERSION 3.20)
project(LingxiBL_STM32 VERSION 1.0.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# STM32N657 specific
set(MCU_FLAGS "-mcpu=cortex-m55 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS "${MCU_FLAGS} -O2 -g -Wall -fdata-sections -ffunction-sections")
set(CMAKE_ASM_FLAGS "${MCU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -T${CMAKE_SOURCE_DIR}/Core/Startup/STM32N657xx_FLASH.ld -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs")

# Include directories
include_directories(
    Core/Inc
    Drivers/STM32N6xx_HAL_Driver/Inc
    Drivers/CMSIS/Include
    Drivers/CMSIS/Device/ST/STM32N6xx/Include
    Middlewares/FreeRTOS/include
    App/inc
    ../shared/protocol
    ../shared/utils
)

# HAL Driver sources
file(GLOB HAL_SOURCES
    Drivers/STM32N6xx_HAL_Driver/Src/*.c
)

# Core sources
set(CORE_SOURCES
    Core/Src/main.c
    Core/Src/stm32n6xx_hal_msp.c
    Core/Src/stm32n6xx_it.c
    Core/Src/system_stm32n6xx.c
    Core/Startup/startup_stm32n657xx.s
)

# Application sources
file(GLOB APP_SOURCES App/src/*.c)

# Shared sources
file(GLOB SHARED_SOURCES ../shared/protocol/*.c ../shared/utils/*.c)

# FreeRTOS sources
file(GLOB FREERTOS_SOURCES Middlewares/FreeRTOS/*.c)

# Executable
add_executable(${PROJECT_NAME}.elf
    ${CORE_SOURCES}
    ${HAL_SOURCES}
    ${APP_SOURCES}
    ${SHARED_SOURCES}
    ${FREERTOS_SOURCES}
)

# Link libraries
find_library(HAL_LIB NAMES stm32n6xx_hal PATHS Drivers/STM32N6xx_HAL_Driver)

# Post-build: Generate hex and bin
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    COMMENT "Generating HEX and BIN files"
)
