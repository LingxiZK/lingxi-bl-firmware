/**
  ******************************************************************************
  * @file    lingxi_bl.h
  * @brief   灵犀智空 BL 感知模组 - 主控头文件
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * STM32N657L0H3Q 主控固件顶层头文件
  * - 硬件抽象层定义
  * - 外设引脚映射
  * - 系统时钟/中断优先级
  ******************************************************************************
  */

#ifndef __LINGXI_BL_H
#define __LINGXI_BL_H

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 标准库头文件
 * ==========================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* =============================================================================
 * HAL 驱动头文件
 * ==========================================================================*/
#include "stm32n6xx_hal.h"

/* =============================================================================
 * FreeRTOS 头文件
 * ==========================================================================*/
/* TODO: 集成 FreeRTOS 时取消注释 */
// #include "FreeRTOS.h"
// #include "task.h"
// #include "semphr.h"
// #include "queue.h"
// #include "timers.h"
// #include "event_groups.h"

/* =============================================================================
 * 版本信息
 * ==========================================================================*/
#define LINGXI_BL_VERSION_MAJOR     3
#define LINGXI_BL_VERSION_MINOR     2
#define LINGXI_BL_VERSION_PATCH     0
#define LINGXI_BL_VERSION_STRING    "v3.2.0"

/* =============================================================================
 * 系统时钟配置 (800MHz HCLK)
 * ==========================================================================*/
#define SYSCLK_FREQ                 800000000U      /* 800 MHz */
#define HCLK_FREQ                   SYSCLK_FREQ
#define PCLK1_FREQ                  200000000U      /* APB1: 200 MHz */
#define PCLK2_FREQ                  200000000U      /* APB2: 200 MHz */
#define PCLK3_FREQ                  200000000U      /* APB3: 200 MHz */

/* =============================================================================
 * 中断优先级分组 (NVIC Priority Group 4: 4 bits preemption, 0 bits sub)
 * ==========================================================================*/
#define NVIC_PRIORITYGROUP          NVIC_PRIORITYGROUP_4

/* 中断优先级定义 (数值越小优先级越高) */
#define IRQ_PRIO_HIGHEST            0   /* NMI, HardFault */
#define IRQ_PRIO_CRITICAL           1   /* SDIO DMA, MIPI CSI-2 */
#define IRQ_PRIO_HIGH               2   /* NPU, DMA */
#define IRQ_PRIO_MEDIUM             3   /* SPI1 (UWB), I2C1 (ToF) */
#define IRQ_PRIO_LOW                4   /* SPI2 (SD), UART, TIM */
#define IRQ_PRIO_LOWEST             5   /* 软件中断, 调试 */

/* =============================================================================
 * GPIO 引脚映射定义
 * ==========================================================================*/

/* ---- MIPI CSI-2 (VD55G1, 单Lane) ----------------------------------------- */
#define MIPI_CSI_D0_PIN             GPIO_PIN_4      /* PH4: CSI_D0+  */
#define MIPI_CSI_D0N_PIN            GPIO_PIN_5      /* PH5: CSI_D0-  */
#define MIPI_CSI_CLK_PIN            GPIO_PIN_6      /* PH6: CSI_CLK+ */
#define MIPI_CSI_CLKN_PIN           GPIO_PIN_7      /* PH7: CSI_CLK- */
#define MIPI_CSI_GPIO_PORT          GPIOH

/* ---- FMC SDRAM (IS42S32160F, 32-bit, 166MHz) ------------------------------ */
/* 数据总线: PD14-PD15, PE0-PE1, PD8-PD10, PE7-PE15, PD0-PD1 */
/* 地址总线: PF0-PF5, PF12-PF15, PG0-PG5 */
/* 控制信号: PC0 (SDNWE), PF11 (SDNRAS), PG15 (SDNCAS), PB5 (SDCKE1), PG8 (SDNE1) */
#define SDRAM_BANK                  FMC_SDRAM_BANK1
#define SDRAM_CMD_TARGET_BANK       FMC_SDRAM_CMD_TARGET_BANK1
#define SDRAM_ROW_BITS              13              /* 8192 rows */
#define SDRAM_COL_BITS              10              /* 1024 columns */
#define SDRAM_BANK_BITS             2               /* 4 banks */
#define SDRAM_DATA_WIDTH            32              /* 32-bit */
#define SDRAM_REFRESH_RATE          8192            /* 64ms / 8192 = 7.81us */
#define SDRAM_SIZE_BYTES            (64 * 1024 * 1024)  /* 64MB */

/* ---- SDIO Host (ESP32-C6 Slave, PC8-PC12 + PD2) --------------------------- */
#define SDIO_CLK_PIN                GPIO_PIN_12     /* PC12: SDIO_CK  */
#define SDIO_CMD_PIN                GPIO_PIN_11     /* PC11: SDIO_CMD */
#define SDIO_D0_PIN                 GPIO_PIN_8      /* PC8:  SDIO_D0  */
#define SDIO_D1_PIN                 GPIO_PIN_9      /* PC9:  SDIO_D1  */
#define SDIO_D2_PIN                 GPIO_PIN_10     /* PC10: SDIO_D2  */
#define SDIO_D3_PIN                 GPIO_PIN_2      /* PD2:  SDIO_D3  */
#define SDIO_GPIO_PORT_CLK          GPIOC
#define SDIO_GPIO_PORT_CMD          GPIOC
#define SDIO_GPIO_PORT_D0_D2        GPIOC
#define SDIO_GPIO_PORT_D3           GPIOD
#define SDIO_IRQn                   SDMMC1_IRQn

/* ---- OctoSPI (W25Q512, 8线) --------------------------------------------- */
#define OSPIM_CLK_PIN               GPIO_PIN_1      /* PB1:  OSPI_CLK */
#define OSPIM_D0_PIN                GPIO_PIN_6      /* PD6:  OSPI_D0  */
#define OSPIM_D1_PIN                GPIO_PIN_7      /* PD7:  OSPI_D1  */
#define OSPIM_D2_PIN                GPIO_PIN_9      /* PE9:  OSPI_D2  */
#define OSPIM_D3_PIN                GPIO_PIN_10     /* PE10: OSPI_D3  */
#define OSPIM_D4_PIN                GPIO_PIN_11     /* PE11: OSPI_D4  */
#define OSPIM_D5_PIN                GPIO_PIN_12     /* PE12: OSPI_D5  */
#define OSPIM_D6_PIN                GPIO_PIN_13     /* PE13: OSPI_D6  */
#define OSPIM_D7_PIN                GPIO_PIN_14     /* PE14: OSPI_D7  */
#define OSPIM_NCS_PIN               GPIO_PIN_6      /* PB6:  OSPI_NCS */
#define OSPIM_GPIO_PORT_CLK         GPIOB
#define OSPIM_GPIO_PORT_D0_D1       GPIOD
#define OSPIM_GPIO_PORT_D2_D7       GPIOE
#define OSPIM_GPIO_PORT_NCS         GPIOB

/* ---- SPI1 (DWM3000 UWB, PA4-PA7 + PA8/PA11/PA12) ------------------------ */
#define UWB_SPI_SCK_PIN             GPIO_PIN_5      /* PA5: SPI1_SCK  */
#define UWB_SPI_MISO_PIN            GPIO_PIN_6      /* PA6: SPI1_MISO */
#define UWB_SPI_MOSI_PIN            GPIO_PIN_7      /* PA7: SPI1_MOSI */
#define UWB_SPI_CS_PIN              GPIO_PIN_4      /* PA4: SPI1_NSS  */
#define UWB_IRQ_PIN                 GPIO_PIN_8      /* PA8: UWB_IRQ   */
#define UWB_RST_PIN                 GPIO_PIN_11     /* PA11: UWB_RST  */
#define UWB_WAKEUP_PIN              GPIO_PIN_12     /* PA12: UWB_WAKEUP */
#define UWB_SPI_GPIO_PORT           GPIOA
#define UWB_SPI                     SPI1
#define UWB_SPI_IRQn                SPI1_IRQn
#define UWB_IRQ_IRQn                EXTI8_IRQn
#define UWB_SPI_BAUDRATE            10000000        /* 10 MHz */

/* ---- SPI2 (BMP585 气压传感器, PB12-PB15 + PD3 INT) ----------------------- */
#define BMP585_SPI_SCK_PIN          GPIO_PIN_13     /* PB13: SPI2_SCK  */
#define BMP585_SPI_MISO_PIN         GPIO_PIN_14     /* PB14: SPI2_MISO */
#define BMP585_SPI_MOSI_PIN         GPIO_PIN_15     /* PB15: SPI2_MOSI */
#define BMP585_SPI_CS_PIN           GPIO_PIN_12     /* PB12: SPI2_NSS  */
#define BMP585_INT_PIN              GPIO_PIN_3      /* PD3:  BMP585_INT */
#define BMP585_SPI_GPIO_PORT        GPIOB
#define BMP585_INT_GPIO_PORT        GPIOD
#define BMP585_SPI                  SPI2
#define BMP585_SPI_IRQn             SPI2_IRQn
#define BMP585_SPI_BAUDRATE         10000000        /* 10 MHz max (BMP585) */
#define BMP585_SPI_GPIO_AF          GPIO_AF5_SPI2

/* ---- SDMMC2 (microSD, PG9-PG14 + PD3 CD) -------------------------------- */
#define SDMMC2_CLK_PIN              GPIO_PIN_9      /* PG9:  SDMMC2_CK  */
#define SDMMC2_CMD_PIN              GPIO_PIN_10     /* PG10: SDMMC2_CMD */
#define SDMMC2_D0_PIN               GPIO_PIN_11     /* PG11: SDMMC2_D0  */
#define SDMMC2_D1_PIN               GPIO_PIN_12     /* PG12: SDMMC2_D1  */
#define SDMMC2_D2_PIN               GPIO_PIN_13     /* PG13: SDMMC2_D2  */
#define SDMMC2_D3_PIN               GPIO_PIN_14     /* PG14: SDMMC2_D3  */
#define SDMMC2_CD_PIN               GPIO_PIN_3      /* PD3:  SD_CD      */
#define SDMMC2_GPIO_PORT            GPIOG
#define SDMMC2_CD_GPIO_PORT         GPIOD
#define SDMMC2_PERIPH               SDMMC2_NS
#define SDMMC2_IRQn                 SDMMC2_IRQn
#define SDMMC2_CLK_GPIO_AF          GPIO_AF11_SDMMC2
#define SDMMC2_GPIO_SPEED           GPIO_SPEED_FREQ_VERY_HIGH

/* ---- I2C3 (VD55G1 摄像头控制, 1.8V, 100kHz) ----------------------------- */
/* 注意：VD55G1 I2C 控制接口电平为 1.8V，需 TXS0102 或 PCA9306 电平转换 */
#define CAM_I2C_SCL_PIN             GPIO_PIN_0      /* PH0: I2C3_SCL (经电平转换) */
#define CAM_I2C_SDA_PIN             GPIO_PIN_1      /* PH1: I2C3_SDA (经电平转换) */
#define CAM_I2C_GPIO_PORT           GPIOH
#define CAM_I2C                     I2C3
#define CAM_I2C_IRQn                I2C3_EV_IRQn
#define CAM_I2C_FREQ                100000          /* 100 kHz */
#define CAM_I2C_ADDR                0x20            /* VD55G1 slave address */

/* ---- JTAG/SWD (PA13/PA14) ----------------------------------------------- */
#define SWDIO_PIN                   GPIO_PIN_13     /* PA13: SWDIO */
#define SWCLK_PIN                   GPIO_PIN_14     /* PA14: SWCLK */
#define SWD_GPIO_PORT               GPIOA

/* ---- SYNC_OUT (同步输出, 预留) ------------------------------------------ */
#define SYNC_OUT_PIN                GPIO_PIN_0      /* 预留GPIO */
#define SYNC_OUT_GPIO_PORT          GPIOI

/* =============================================================================
 * 错误码定义 (统一错误处理)
 * ==========================================================================*/
typedef enum {
    LINGXI_OK                   = 0,    /* 成功 */
    LINGXI_ERR_GENERIC          = -1,   /* 通用错误 */
    LINGXI_ERR_INVALID_PARAM    = -2,   /* 参数无效 */
    LINGXI_ERR_NULL_PTR         = -3,   /* 空指针 */
    LINGXI_ERR_TIMEOUT          = -4,   /* 超时 */
    LINGXI_ERR_BUSY             = -5,   /* 设备忙 */
    LINGXI_ERR_NOT_INIT         = -6,   /* 未初始化 */
    LINGXI_ERR_NO_MEM           = -7,   /* 内存不足 */
    LINGXI_ERR_IO               = -8,   /* IO错误 */
    LINGXI_ERR_CRC              = -9,   /* CRC校验失败 */
    LINGXI_ERR_DMA              = -10,  /* DMA错误 */
    LINGXI_ERR_ISR              = -11,  /* 中断错误 */
    LINGXI_ERR_NPU              = -12,  /* NPU错误 */
    LINGXI_ERR_MODEL            = -13,  /* 模型错误 */
    LINGXI_ERR_COMM             = -14,  /* 通信错误 */
    LINGXI_ERR_SENSOR           = -15,  /* 传感器错误 */
} lingxi_err_t;

/* =============================================================================
 * 断言与调试宏
 * ==========================================================================*/
#ifdef DEBUG
    #define LX_ASSERT(cond)         assert(cond)
    #define LX_DEBUG_PRINT(fmt, ...) printf("[DBG] " fmt "\r\n", ##__VA_ARGS__)
    #define LX_ERR_PRINT(fmt, ...)   printf("[ERR] %s:%d " fmt "\r\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define LX_ASSERT(cond)         ((void)0)
    #define LX_DEBUG_PRINT(fmt, ...) ((void)0)
    #define LX_ERR_PRINT(fmt, ...)   ((void)0)
#endif

#define LX_RETURN_IF_NULL(ptr)      do { if ((ptr) == NULL) return LINGXI_ERR_NULL_PTR; } while(0)
#define LX_RETURN_IF_ERR(err)       do { if ((err) != LINGXI_OK) return (err); } while(0)
#define LX_GOTO_IF_ERR(err, label)  do { if ((err) != LINGXI_OK) goto label; } while(0)

/* =============================================================================
 * 编译期检查
 * ==========================================================================*/
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

/* 确保数据结构对齐 */
#define LX_ALIGN(x) __attribute__((aligned(x)))

/* 中断安全函数属性 */
#define LX_ISR_FUNC __attribute__((section(".ramfunc")))

/* =============================================================================
 * 全局事件标志位 (FreeRTOS EventGroup)
 * ==========================================================================*/
#define EVT_SYS_INIT_DONE           (1U << 0)   /* 系统初始化完成 */
#define EVT_CAM_FRAME_READY         (1U << 0)   /* 摄像头帧就绪 */
#define EVT_NPU_INFER_DONE          (1U << 1)   /* NPU推理完成 */
#define EVT_SENSOR_DATA_READY       (1U << 2)   /* 传感器数据就绪 */
#define EVT_COMM_TX_READY           (1U << 3)   /* 通信发送就绪 */
#define EVT_COMM_RX_READY           (1U << 4)   /* 通信接收就绪 */
#define EVT_OBSTACLE_DETECTED       (1U << 5)   /* 检测到障碍物 */
#define EVT_EDGE_DETECTED           (1U << 6)   /* 检测到边缘 */
#define EVT_TRACK_TARGET_LOST       (1U << 7)   /* 跟踪目标丢失 */

/* =============================================================================
 * 数据类型别名 (便于跨平台)
 * ==========================================================================*/
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef float    f32;

#ifdef __cplusplus
}
#endif

#endif /* __LINGXI_BL_H */
