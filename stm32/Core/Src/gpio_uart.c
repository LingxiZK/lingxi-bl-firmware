/**
  ******************************************************************************
  * @file    gpio_uart.c
  * @brief   GPIO 和 UART 调试接口实现 (STM32N657L0H3Q) — P0批次
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - UART1: PA9(TX) / PA10(RX), 115200bps
  * - 所有外设 GPIO 预初始化
  * - SWD 调试口保持默认功能
  ******************************************************************************
  */

#include "gpio_uart.h"
#include "rcc_config.h"
#include "stm32n6xx_hal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* =============================================================================
 * 私有宏定义
 * ==========================================================================*/
#define UART_TX_BUF_SIZE            512
#define UART_RX_BUF_SIZE            256
#define UART_TX_TIMEOUT_MS          100
#define UART_RX_TIMEOUT_MS          100

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static UART_HandleTypeDef huart1;
static uint8_t s_uart_tx_buf[UART_TX_BUF_SIZE];
static uint8_t s_uart_rx_buf[UART_RX_BUF_SIZE];
static volatile uint8_t s_uart_initialized = 0;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static int uart1_gpio_init(void);
static int uart1_periph_init(void);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化所有 GPIO 和 UART
 */
int gpio_init_all(void)
{
    /* 使能所有 GPIO 端口时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    /* N6: GPIOI not available */
    // __HAL_RCC_GPIOI_CLK_ENABLE();

    /* 初始化 SWD 调试口 */
    swd_debug_init();

    /* 初始化 UART1 调试串口 */
    if (uart1_debug_init() != 0) {
        return -1;
    }

    /* 初始化各外设 GPIO */
    gpio_mipi_csi_init();
    gpio_sdio_init();
    gpio_sdmmc2_init();
    gpio_fmc_sdram_init();
    gpio_ospi_init();
    gpio_spi1_uwb_init();
    gpio_bmp585_init();
    gpio_misc_init();

    DBG_PRINT("GPIO init complete: all peripherals configured");
    return 0;
}

/**
 * @brief  初始化 UART1 调试串口 (115200bps)
 */
int uart1_debug_init(void)
{
    HAL_StatusTypeDef status;

    /* 使能 UART1 时钟 */
    __HAL_RCC_USART1_CLK_ENABLE();

    /* GPIO 初始化 */
    if (uart1_gpio_init() != 0) {
        return -1;
    }

    /* UART 外设初始化 */
    if (uart1_periph_init() != 0) {
        return -1;
    }

    s_uart_initialized = 1;

    DBG_PRINT("UART1 init: %lu baud", UART1_BAUDRATE);
    return 0;
}

/**
 * @brief  通过 UART1 发送字符串
 */
int uart1_send_string(const char *str)
{
    if (!s_uart_initialized || str == NULL) {
        return -1;
    }

    uint16_t len = (uint16_t)strlen(str);
    if (len == 0) {
        return 0;
    }

    HAL_StatusTypeDef status = HAL_UART_Transmit(
        &huart1,
        (uint8_t *)str,
        len,
        UART_TX_TIMEOUT_MS
    );

    return (status == HAL_OK) ? 0 : -1;
}

/**
 * @brief  通过 UART1 发送数据
 */
int uart1_send_data(const uint8_t *data, uint16_t len)
{
    if (!s_uart_initialized || data == NULL || len == 0) {
        return -1;
    }

    HAL_StatusTypeDef status = HAL_UART_Transmit(
        &huart1,
        (uint8_t *)data,
        len,
        UART_TX_TIMEOUT_MS
    );

    return (status == HAL_OK) ? 0 : -1;
}

/**
 * @brief  通过 UART1 接收数据 (非阻塞)
 */
int uart1_recv_data(uint8_t *data, uint16_t max_len)
{
    if (!s_uart_initialized || data == NULL || max_len == 0) {
        return -1;
    }

    HAL_StatusTypeDef status = HAL_UART_Receive(
        &huart1,
        data,
        max_len,
        UART_RX_TIMEOUT_MS
    );

    if (status == HAL_OK) {
        return (int)max_len;
    } else if (status == HAL_TIMEOUT) {
        /* 返回实际接收到的字节数 */
        return huart1.RxXferSize - huart1.RxXferCount;
    }

    return -1;
}

/**
 * @brief  格式化输出到调试串口
 */
int gpio_uart_printf(const char *fmt, ...)
{
    if (!s_uart_initialized) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf((char *)s_uart_tx_buf, UART_TX_BUF_SIZE, fmt, args);
    va_end(args);

    if (len < 0 || len >= UART_TX_BUF_SIZE) {
        return -1;
    }

    HAL_UART_Transmit(&huart1, s_uart_tx_buf, (uint16_t)len, UART_TX_TIMEOUT_MS);
    return len;
}

/**
 * @brief  配置 SWD 调试口 (PA13/PA14)
 */
int swd_debug_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PA13: SWDIO, PA14: SWCLK - 保持默认 AF0 (SWD) */
    GPIO_InitStruct.Pin = SWDIO_PIN | SWCLK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF0_SWJ;
    HAL_GPIO_Init(SWD_GPIO_PORT, &GPIO_InitStruct);

    return 0;
}

/**
 * @brief  配置 MIPI CSI-2 GPIO
 */
int gpio_mipi_csi_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PH4-PH7: MIPI CSI-2 D0/D0N/CLK/CLKN */
    GPIO_InitStruct.Pin = MIPI_CSI_D0_PIN | MIPI_CSI_D0N_PIN |
                          MIPI_CSI_CLK_PIN | MIPI_CSI_CLKN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = MIPI_CSI_GPIO_AF;
    HAL_GPIO_Init(MIPI_CSI_GPIO_PORT, &GPIO_InitStruct);

    return 0;
}

/**
 * @brief  配置 SDIO GPIO
 */
int gpio_sdio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PC8-PC12: SDIO D0-D3, CLK, CMD */
    GPIO_InitStruct.Pin = SDIO_D0_PIN | SDIO_D1_PIN | SDIO_D2_PIN |
                          SDIO_CLK_PIN | SDIO_CMD_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = SDIO_GPIO_AF;
    HAL_GPIO_Init(SDIO_GPIO_PORT_CLK_CMD_D02, &GPIO_InitStruct);

    /* PD2: SDIO D3 */
    GPIO_InitStruct.Pin = SDIO_D3_PIN;
    HAL_GPIO_Init(SDIO_GPIO_PORT_D3, &GPIO_InitStruct);

    return 0;
}

/**
 * @brief  配置 FMC SDRAM GPIO
 */
int gpio_fmc_sdram_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 数据总线 */
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = SDRAM_GPIO_AF;

    /* PD0-PD1, PD8-PD10, PD14-PD15 */
    GPIO_InitStruct.Pin = SDRAM_DATA_PINS_PORTD;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* PE7-PE15 */
    GPIO_InitStruct.Pin = SDRAM_DATA_PINS_PORTE;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* 地址总线 */
    /* PF0-PF5, PF12-PF15 */
    GPIO_InitStruct.Pin = SDRAM_ADDR_PINS_PORTF;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    /* PG0-PG5 */
    GPIO_InitStruct.Pin = SDRAM_ADDR_PINS_PORTG;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* 控制信号 */
    /* PC0: SDNWE */
    GPIO_InitStruct.Pin = SDRAM_CTRL_SDNWE_PIN;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PF11: SDNRAS */
    GPIO_InitStruct.Pin = SDRAM_CTRL_SDNRAS_PIN;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    /* PG15: SDNCAS */
    GPIO_InitStruct.Pin = SDRAM_CTRL_SDNCAS_PIN;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* PB5: SDCKE1 */
    GPIO_InitStruct.Pin = SDRAM_CTRL_SDCKE1_PIN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PG8: SDNE1 */
    GPIO_InitStruct.Pin = SDRAM_CTRL_SDNE1_PIN;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    return 0;
}

/**
 * @brief  配置 OctoSPI GPIO
 */
int gpio_ospi_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PB1: CLK, PB6: NCS */
    GPIO_InitStruct.Pin = OSPIM_CLK_PIN | OSPIM_NCS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = OSPIM_GPIO_AF;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PD6-PD7: D0-D1 */
    GPIO_InitStruct.Pin = OSPIM_D0_PIN | OSPIM_D1_PIN;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* PE9-PE14: D2-D7 */
    GPIO_InitStruct.Pin = OSPIM_D2_PIN | OSPIM_D3_PIN | OSPIM_D4_PIN |
                          OSPIM_D5_PIN | OSPIM_D6_PIN | OSPIM_D7_PIN;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    return 0;
}

/**
 * @brief  配置 SPI1 (UWB) GPIO
 */
int gpio_spi1_uwb_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PA4: CS, PA5: SCK, PA6: MISO, PA7: MOSI */
    GPIO_InitStruct.Pin = UWB_SPI_CS_PIN | UWB_SPI_SCK_PIN |
                          UWB_SPI_MISO_PIN | UWB_SPI_MOSI_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = UWB_SPI_GPIO_AF;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA8: IRQ (输入中断) */
    GPIO_InitStruct.Pin = UWB_IRQ_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA11: RST, PA12: WAKEUP (输出) */
    GPIO_InitStruct.Pin = UWB_RST_PIN | UWB_WAKEUP_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 默认状态: RST=1, WAKEUP=0 */
    HAL_GPIO_WritePin(GPIOA, UWB_RST_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, UWB_WAKEUP_PIN, GPIO_PIN_RESET);

    return 0;
}

/**
 * @brief  配置 SPI2 (BMP585) GPIO
 */
int gpio_bmp585_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /* PB13: SCK, PB14: MISO, PB15: MOSI */
    GPIO_InitStruct.Pin = BMP585_SPI_SCK_PIN | BMP585_SPI_MISO_PIN | BMP585_SPI_MOSI_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = BMP585_SPI_GPIO_AF;
    HAL_GPIO_Init(BMP585_SPI_GPIO_PORT, &GPIO_InitStruct);

    /* PB12: CS (软件控制) */
    GPIO_InitStruct.Pin = BMP585_SPI_CS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BMP585_SPI_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(BMP585_SPI_GPIO_PORT, BMP585_SPI_CS_PIN, GPIO_PIN_SET);

    /* PD3: INT (输入中断) */
    GPIO_InitStruct.Pin = BMP585_INT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(BMP585_INT_GPIO_PORT, &GPIO_InitStruct);

    return 0;
}

/**
 * @brief  配置 SDMMC2 (microSD) GPIO
 */
int gpio_sdmmc2_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDMMC2_CLK_ENABLE();

    /* PG9: CK, PG10: CMD, PG11-PG14: D0-D3 */
    GPIO_InitStruct.Pin = SDMMC2_CLK_PIN | SDMMC2_CMD_PIN |
                          SDMMC2_D0_PIN | SDMMC2_D1_PIN |
                          SDMMC2_D2_PIN | SDMMC2_D3_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = SDMMC2_GPIO_SPEED;
    GPIO_InitStruct.Alternate = SDMMC2_GPIO_AF;
    HAL_GPIO_Init(SDMMC2_GPIO_PORT, &GPIO_InitStruct);

    /* PD3: CD (卡检测, 输入) */
    GPIO_InitStruct.Pin = SDMMC2_CD_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SDMMC2_CD_GPIO_PORT, &GPIO_InitStruct);

    return 0;
}

/**
 * @brief  配置独立 GPIO (LED/按键/同步输出)
 */
int gpio_misc_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 预留 GPIOI 引脚 */
    /* N6: GPIOI not available */
    // __HAL_RCC_GPIOI_CLK_ENABLE();

    /* 配置为输出 (预留 LED/状态指示) */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    /* N6: GPIOI not available, skipping */
    // /* HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

    /* 默认低电平 */
    // HAL_GPIO_WritePin(GPIOI, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_RESET);

    return 0;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  UART1 GPIO 初始化
 */
static int uart1_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PA9: TX, PA10: RX */
    GPIO_InitStruct.Pin = UART1_TX_PIN | UART1_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = UART1_GPIO_AF;
    HAL_GPIO_Init(UART1_GPIO_PORT, &GPIO_InitStruct);

    return 0;
}

/**
 * @brief  UART1 外设初始化
 */
static int uart1_periph_init(void)
{
    HAL_StatusTypeDef status;

    huart1.Instance = USART1;
    huart1.Init.BaudRate = UART1_BAUDRATE;
    huart1.Init.WordLength = UART1_DATABITS;
    huart1.Init.StopBits = UART1_STOPBITS;
    huart1.Init.Parity = UART1_PARITY;
    huart1.Init.Mode = UART1_MODE;
    huart1.Init.HwFlowCtl = UART1_HWFLOW;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    status = HAL_UART_Init(&huart1);
    if (status != HAL_OK) {
        return -1;
    }

    return 0;
}
