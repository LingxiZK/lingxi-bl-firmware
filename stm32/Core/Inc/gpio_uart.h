/**
  ******************************************************************************
  * @file    gpio_uart.h
  * @brief   GPIO 和 UART 调试接口头文件 (STM32N657L0H3Q) — P0批次
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - UART1: PA9(TX) / PA10(RX), 115200bps, 调试串口
  * - SWD: PA13(SWDIO) / PA14(SWCLK)
  * - 各外设 GPIO 预配置 (MIPI/SDIO/SPI/I2C)
  ******************************************************************************
  */

#ifndef __GPIO_UART_H
#define __GPIO_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * UART1 配置参数
 * ==========================================================================*/
#define UART1_BAUDRATE              115200U
#define UART1_DATABITS              UART_WORDLENGTH_8B
#define UART1_STOPBITS              UART_STOPBITS_1
#define UART1_PARITY                UART_PARITY_NONE
#define UART1_HWFLOW                UART_HWCONTROL_NONE
#define UART1_MODE                  UART_MODE_TX_RX

/* UART1 引脚 */
#define UART1_TX_PIN                GPIO_PIN_9      /* PA9: USART1_TX */
#define UART1_RX_PIN                GPIO_PIN_10     /* PA10: USART1_RX */
#define UART1_GPIO_PORT             GPIOA
#define UART1_GPIO_AF               GPIO_AF7_USART1

/* =============================================================================
 * SWD 调试口引脚
 * ==========================================================================*/
#define SWDIO_PIN                   GPIO_PIN_13     /* PA13: SWDIO */
#define SWCLK_PIN                   GPIO_PIN_14     /* PA14: SWCLK */
#define SWD_GPIO_PORT               GPIOA

/* =============================================================================
 * 外设 GPIO 预定义 (根据 lingxi_bl.h 引脚映射)
 * ==========================================================================*/

/* MIPI CSI-2 (VD55G1) */
#define MIPI_CSI_D0_PIN             GPIO_PIN_4      /* PH4 */
#define MIPI_CSI_D0N_PIN            GPIO_PIN_5      /* PH5 */
#define MIPI_CSI_CLK_PIN            GPIO_PIN_6      /* PH6 */
#define MIPI_CSI_CLKN_PIN           GPIO_PIN_7      /* PH7 */
#define MIPI_CSI_GPIO_PORT          GPIOH
/* N6: MIPI CSI-2 is dedicated PHY, not standard GPIO AF */
#define MIPI_CSI_GPIO_AF            0xFF  /* Placeholder: CSI uses dedicated PHY pins */

/* SDIO (SDMMC1 主机 - ESP32-C6 Slave, PC8-PC12 + PD2) */
#define SDIO_CLK_PIN                GPIO_PIN_12     /* PC12: SDIO_CK */
#define SDIO_CMD_PIN                GPIO_PIN_11     /* PC11: SDIO_CMD */
#define SDIO_D0_PIN                 GPIO_PIN_8      /* PC8: SDIO_D0 */
#define SDIO_D1_PIN                 GPIO_PIN_9      /* PC9: SDIO_D1 */
#define SDIO_D2_PIN                 GPIO_PIN_10     /* PC10: SDIO_D2 */
#define SDIO_D3_PIN                 GPIO_PIN_2      /* PD2: SDIO_D3 */
#define SDIO_GPIO_PORT_CLK_CMD_D02  GPIOC
#define SDIO_GPIO_PORT_D3           GPIOD
#define SDIO_GPIO_AF                GPIO_AF12_SDMMC1

/* SDMMC2 (microSD, PG9-PG14 + PD3 CD) */
#define SDMMC2_CLK_PIN              GPIO_PIN_9      /* PG9:  SDMMC2_CK  */
#define SDMMC2_CMD_PIN              GPIO_PIN_10     /* PG10: SDMMC2_CMD */
#define SDMMC2_D0_PIN               GPIO_PIN_11     /* PG11: SDMMC2_D0  */
#define SDMMC2_D1_PIN               GPIO_PIN_12     /* PG12: SDMMC2_D1  */
#define SDMMC2_D2_PIN               GPIO_PIN_13     /* PG13: SDMMC2_D2  */
#define SDMMC2_D3_PIN               GPIO_PIN_14     /* PG14: SDMMC2_D3  */
#define SDMMC2_CD_PIN               GPIO_PIN_3      /* PD3:  SD_CD      */
#define SDMMC2_GPIO_PORT            GPIOG
#define SDMMC2_CD_GPIO_PORT         GPIOD
#define SDMMC2_GPIO_AF              GPIO_AF11_SDMMC2
#define SDMMC2_GPIO_SPEED           GPIO_SPEED_FREQ_VERY_HIGH

/* BMP585 (气压传感器, SPI2, PB12-PB15 + PD3 INT) */
#define BMP585_SPI_SCK_PIN          GPIO_PIN_13     /* PB13: SPI2_SCK  */
#define BMP585_SPI_MISO_PIN         GPIO_PIN_14     /* PB14: SPI2_MISO */
#define BMP585_SPI_MOSI_PIN         GPIO_PIN_15     /* PB15: SPI2_MOSI */
#define BMP585_SPI_CS_PIN           GPIO_PIN_12     /* PB12: SPI2_NSS  */
#define BMP585_INT_PIN              GPIO_PIN_3      /* PD3:  BMP585_INT */
#define BMP585_SPI_GPIO_PORT        GPIOB
#define BMP585_INT_GPIO_PORT        GPIOD
#define BMP585_SPI_GPIO_AF          GPIO_AF5_SPI2

/* VD55G1 (I2C3, 1.8V 电平，需电平转换) */
#define CAM_I2C_SCL_PIN             GPIO_PIN_0      /* PH0: I2C3_SCL */
#define CAM_I2C_SDA_PIN             GPIO_PIN_1      /* PH1: I2C3_SDA */
#define CAM_I2C_GPIO_PORT           GPIOH
#define CAM_I2C_GPIO_AF             GPIO_AF4_I2C3

/* FMC SDRAM (IS42S32160F, 32-bit, 166MHz) */
#define SDRAM_DATA_PINS_PORTD       (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | \
                                     GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15)
#define SDRAM_DATA_PINS_PORTE       (GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | \
                                     GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | \
                                     GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)
#define SDRAM_ADDR_PINS_PORTF       (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | \
                                     GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | \
                                     GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)
#define SDRAM_ADDR_PINS_PORTG       (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | \
                                     GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5)
#define SDRAM_CTRL_SDNWE_PIN        GPIO_PIN_0      /* PC0 */
#define SDRAM_CTRL_SDNRAS_PIN       GPIO_PIN_11     /* PF11 */
#define SDRAM_CTRL_SDNCAS_PIN       GPIO_PIN_15     /* PG15 */
#define SDRAM_CTRL_SDCKE1_PIN       GPIO_PIN_5      /* PB5 */
#define SDRAM_CTRL_SDNE1_PIN        GPIO_PIN_8      /* PG8 */
#define SDRAM_GPIO_AF               GPIO_AF12_FMC

/* OctoSPI (W25Q512, 8线) */
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
#define OSPIM_GPIO_AF               GPIO_AF9_XSPIM_P1  /* N6: AF9 for XSPIM */

/* SPI1 (DWM3000 UWB) */
#define UWB_SPI_SCK_PIN             GPIO_PIN_5      /* PA5: SPI1_SCK  */
#define UWB_SPI_MISO_PIN            GPIO_PIN_6      /* PA6: SPI1_MISO */
#define UWB_SPI_MOSI_PIN            GPIO_PIN_7      /* PA7: SPI1_MOSI */
#define UWB_SPI_CS_PIN              GPIO_PIN_4      /* PA4: SPI1_NSS  */
#define UWB_IRQ_PIN                 GPIO_PIN_8      /* PA8: UWB_IRQ   */
#define UWB_RST_PIN                 GPIO_PIN_11     /* PA11: UWB_RST  */
#define UWB_WAKEUP_PIN              GPIO_PIN_12     /* PA12: UWB_WAKEUP */
#define UWB_SPI_GPIO_AF             GPIO_AF5_SPI1

/* =============================================================================
 * 调试输出宏
 * ==========================================================================*/
#ifdef DEBUG
    #define DBG_PRINT(fmt, ...)     gpio_uart_printf("[DBG] " fmt "\r\n", ##__VA_ARGS__)
    #define ERR_PRINT(fmt, ...)     gpio_uart_printf("[ERR] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define DBG_PRINT(fmt, ...)     ((void)0)
    #define ERR_PRINT(fmt, ...)     ((void)0)
#endif

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化所有 GPIO (包括外设复用和调试口)
 * @retval 0 成功, -1 失败
 */
int gpio_init_all(void);

/**
 * @brief  初始化 UART1 调试串口 (115200bps)
 * @retval 0 成功, -1 失败
 */
int uart1_debug_init(void);

/**
 * @brief  通过 UART1 发送字符串
 * @param  str: 字符串指针
 * @retval 0 成功
 */
int uart1_send_string(const char *str);

/**
 * @brief  通过 UART1 发送数据
 * @param  data: 数据缓冲区
 * @param  len: 数据长度
 * @retval 0 成功, -1 失败
 */
int uart1_send_data(const uint8_t *data, uint16_t len);

/**
 * @brief  通过 UART1 接收数据 (非阻塞)
 * @param  data: 接收缓冲区
 * @param  max_len: 最大接收长度
 * @retval 实际接收字节数
 */
int uart1_recv_data(uint8_t *data, uint16_t max_len);

/**
 * @brief  格式化输出到调试串口 (类似 printf)
 * @param  fmt: 格式字符串
 * @retval 输出字符数
 */
int gpio_uart_printf(const char *fmt, ...);

/**
 * @brief  配置 SWD 调试口 (PA13/PA14)
 * @retval 0 成功
 */
int swd_debug_init(void);

/**
 * @brief  配置 MIPI CSI-2 GPIO
 * @retval 0 成功
 */
int gpio_mipi_csi_init(void);

/**
 * @brief  配置 SDIO GPIO
 * @retval 0 成功
 */
int gpio_sdio_init(void);

/**
 * @brief  配置 FMC SDRAM GPIO
 * @retval 0 成功
 */
int gpio_fmc_sdram_init(void);

/**
 * @brief  配置 OctoSPI GPIO
 * @retval 0 成功
 */
int gpio_ospi_init(void);

/**
 * @brief  配置 SPI1 (UWB) GPIO
 * @retval 0 成功
 */
int gpio_spi1_uwb_init(void);

/**
 * @brief  配置 SPI2 (BMP585 气压) GPIO
 * @retval 0 成功
 */
int gpio_bmp585_init(void);

/**
 * @brief  配置 SDMMC2 (microSD) GPIO
 * @retval 0 成功
 */
int gpio_sdmmc2_init(void);

/**
 * @brief  配置独立 GPIO (LED/按键等)
 * @retval 0 成功
 */
int gpio_misc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __GPIO_UART_H */
