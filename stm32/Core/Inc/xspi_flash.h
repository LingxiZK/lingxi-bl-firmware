/**
  ******************************************************************************
  * @file    xspi_flash.h
  * @brief   XSPI1 OctoSPI Flash HAL Driver Header (W25Q512, STM32N657)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - Target: STM32N657L0H3Q, XSPI1 (OctoSPI)
  * - Flash: W25Q512JV, 128Mbit = 64MB
  * - Mode: OctoSPI DTR (Double Transfer Rate)
  * - Clock: 100-133MHz
  * - Pin Mapping:
  *     CLK:  PB2
  *     D0-D7: PB0/PB1/PC13/PC2/PC3/PC4/PD11/PD12
  *     NCS:  PB6
  * - Features: XIP, Auto Erase, Page Program, Fast Read
  ******************************************************************************
  */

#ifndef __XSPI_FLASH_H
#define __XSPI_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * 版本与标识
 * ==========================================================================*/
#define XSPI_FLASH_DRIVER_VERSION   0x0320  /* v3.2.0 */

/* =============================================================================
 * W25Q512 容量与分区参数
 * ==========================================================================*/
#define XSPI_FLASH_SIZE             (16U * 1024U * 1024U)   /* 16 MB */
#define XSPI_FLASH_SECTOR_SIZE      (4U * 1024U)            /* 4 KB sector */
#define XSPI_FLASH_BLOCK_SIZE       (64U * 1024U)           /* 64 KB block */
#define XSPI_FLASH_PAGE_SIZE        256U                    /* 256 bytes/page */
#define XSPI_FLASH_SECTOR_COUNT     (XSPI_FLASH_SIZE / XSPI_FLASH_SECTOR_SIZE)
#define XSPI_FLASH_BLOCK_COUNT      (XSPI_FLASH_SIZE / XSPI_FLASH_BLOCK_SIZE)
#define XSPI_FLASH_PAGE_COUNT       (XSPI_FLASH_SIZE / XSPI_FLASH_PAGE_SIZE)

/* =============================================================================
 * W25Q512 指令集
 * ==========================================================================*/
/* 基本指令 (1-line) */
#define W25Q_CMD_RDID               0x9F    /* Read JEDEC ID */
#define W25Q_CMD_WREN               0x06    /* Write Enable */
#define W25Q_CMD_WRDI               0x04    /* Write Disable */
#define W25Q_CMD_RDSR1              0x05    /* Read Status Register 1 */
#define W25Q_CMD_RDSR2              0x35    /* Read Status Register 2 */
#define W25Q_CMD_RDSR3              0x15    /* Read Status Register 3 */
#define W25Q_CMD_WRSR1              0x01    /* Write Status Register 1 */
#define W25Q_CMD_WRSR2              0x31    /* Write Status Register 2 */
#define W25Q_CMD_WRSR3              0x11    /* Write Status Register 3 */
#define W25Q_CMD_EN4B               0xB7    /* Enter 4-Byte Address Mode */
#define W25Q_CMD_EX4B               0xE9    /* Exit 4-Byte Address Mode */

/* 读取指令 */
#define W25Q_CMD_READ               0x03    /* Read Data (1-1-1, slow) */
#define W25Q_CMD_FAST_READ          0x0B    /* Fast Read (1-1-1) */
#define W25Q_CMD_4READ             0x6B    /* Quad Output Fast Read (1-1-4) */
#define W25Q_CMD_8READ             0xEC    /* Octal Word Read (8-8-8, DTR) */
#define W25Q_CMD_8READ_DTR         0xEE    /* Octal Word Read DTR (8-8-8, DTR) */

/* 写入指令 */
#define W25Q_CMD_PP                 0x02    /* Page Program (1-1-1) */
#define W25Q_CMD_4PP               0x32    /* Quad Page Program (1-1-4) */
#define W25Q_CMD_8PP               0xC0    /* Octal Page Program (8-8-8) */
#define W25Q_CMD_8PP_DTR           0xC2    /* Octal Page Program DTR (8-8-8, DTR) */

/* 擦除指令 */
#define W25Q_CMD_SE                 0x20    /* Sector Erase 4KB (1-0-0) */
#define W25Q_CMD_BE32               0x52    /* Block Erase 32KB */
#define W25Q_CMD_BE                 0xD8    /* Block Erase 64KB */
#define W25Q_CMD_CE                 0xC7    /* Chip Erase (或 0x60) */

/* 复位指令 */
#define W25Q_CMD_EN_RST             0x66    /* Enable Reset */
#define W25Q_CMD_RST_DEV            0x99    /* Reset Device */
#define W25Q_CMD_SRSTEN             0x66    /* Software Reset Enable */
#define W25Q_CMD_SRST               0xF0    /* Software Reset */

/* 状态寄存器位 */
#define W25Q_SR1_WIP                0x01    /* Write In Progress */
#define W25Q_SR1_WEL                0x02    /* Write Enable Latch */
#define W25Q_SR1_BP0                0x04    /* Block Protect 0 */
#define W25Q_SR1_BP1                0x08    /* Block Protect 1 */
#define W25Q_SR1_BP2                0x10    /* Block Protect 2 */
#define W25Q_SR1_BP3                0x20    /* Block Protect 3 */
#define W25Q_SR1_QE                 0x40    /* Quad Enable (SR2 bit) */
#define W25Q_SR1_SRP                0x80    /* Status Register Protect */

/* =============================================================================
 * GPIO 引脚定义 (XSPI1)
 * ==========================================================================*/
#define XSPI1_CLK_PIN               GPIO_PIN_2      /* PB2:  XSPI_CLK */
#define XSPI1_D0_PIN                GPIO_PIN_0      /* PB0:  XSPI_D0  */
#define XSPI1_D1_PIN                GPIO_PIN_1      /* PB1:  XSPI_D1  */
#define XSPI1_D2_PIN                GPIO_PIN_13     /* PC13: XSPI_D2  */
#define XSPI1_D3_PIN                GPIO_PIN_2      /* PC2:  XSPI_D3  */
#define XSPI1_D4_PIN                GPIO_PIN_3      /* PC3:  XSPI_D4  */
#define XSPI1_D5_PIN                GPIO_PIN_4      /* PC4:  XSPI_D5  */
#define XSPI1_D6_PIN                GPIO_PIN_11     /* PD11: XSPI_D6  */
#define XSPI1_D7_PIN                GPIO_PIN_12     /* PD12: XSPI_D7  */
#define XSPI1_NCS_PIN               GPIO_PIN_6      /* PB6:  XSPI_NCS */

#define XSPI1_GPIO_PORT_CLK         GPIOB
#define XSPI1_GPIO_PORT_D0D1       GPIOB
#define XSPI1_GPIO_PORT_D2D5        GPIOC
#define XSPI1_GPIO_PORT_D6D7        GPIOD
#define XSPI1_GPIO_PORT_NCS         GPIOB

#define XSPI1_GPIO_AF               GPIO_AF10_XSPI1

/* =============================================================================
 * 时钟与时序参数
 * ==========================================================================*/
#define XSPI1_TARGET_CLK_MHZ        133U        /* 目标时钟 133MHz */
#define XSPI1_PRESCALER             2U          /* HCLK分频, 800/6 ≈ 133MHz (实际按HAL配置) */
#define XSPI1_DUMMY_CYCLES_READ     6U          /* 读取dummy cycles */
#define XSPI1_DUMMY_CYCLES_DTR      4U          /* DTR模式dummy cycles */

/* 超时配置 */
#define XSPI_FLASH_INIT_TIMEOUT_MS  1000U
#define XSPI_FLASH_CMD_TIMEOUT_MS   1000U
#define XSPI_FLASH_ERASE_SECTOR_MS  400U        /* 扇区擦除最大400ms */
#define XSPI_FLASH_ERASE_BLOCK_MS   2000U       /* 块擦除最大2s */
#define XSPI_FLASH_ERASE_CHIP_MS    20000U      /* 整片擦除最大20s */
#define XSPI_FLASH_WRITE_PAGE_MS    3U          /* 页写入最大3ms */
#define XSPI_FLASH_WIP_TIMEOUT_MS   5000U       /* WIP轮询超时 */
#define XSPI_FLASH_WIP_POLL_US      10U         /* WIP轮询间隔 */

/* =============================================================================
 * 错误码定义
 * ==========================================================================*/
typedef enum {
    XSPI_FLASH_OK = 0,
    XSPI_FLASH_ERR_NULL_PTR = -1,
    XSPI_FLASH_ERR_INVALID_PARAM = -2,
    XSPI_FLASH_ERR_TIMEOUT = -3,
    XSPI_FLASH_ERR_HW_INIT = -4,
    XSPI_FLASH_ERR_ID_MISMATCH = -5,
    XSPI_FLASH_ERR_NOT_INIT = -6,
    XSPI_FLASH_ERR_BUSY = -7,
    XSPI_FLASH_ERR_WRITE_PROTECT = -8,
    XSPI_FLASH_ERR_ERASE = -9,
    XSPI_FLASH_ERR_PROGRAM = -10,
    XSPI_FLASH_ERR_XIP = -11,
    XSPI_FLASH_ERR_COMM = -12,
} xspi_flash_err_t;

/* =============================================================================
 * 驱动状态枚举
 * ==========================================================================*/
typedef enum {
    XSPI_FLASH_STATE_RESET = 0,
    XSPI_FLASH_STATE_INIT,
    XSPI_FLASH_STATE_READY,
    XSPI_FLASH_STATE_XIP,
    XSPI_FLASH_STATE_BUSY,
    XSPI_FLASH_STATE_ERROR,
} xspi_flash_state_t;

/* =============================================================================
 * 操作模式枚举
 * ==========================================================================*/
typedef enum {
    XSPI_FLASH_MODE_1LINE = 0,      /* 1-1-1 标准SPI */
    XSPI_FLASH_MODE_4LINE,         /* 1-1-4 Quad SPI */
    XSPI_FLASH_MODE_8LINE,         /* 8-8-8 OctoSPI STR */
    XSPI_FLASH_MODE_8LINE_DTR,     /* 8-8-8 OctoSPI DTR */
} xspi_flash_mode_t;

/* =============================================================================
 * Flash ID 结构体
 * ==========================================================================*/
typedef struct {
    uint8_t manufacturer_id;        /* 厂商ID (Winbond=0xEF) */
    uint8_t memory_type;            /* 存储器类型 */
    uint8_t capacity;               /* 容量编码 */
    uint32_t jedec_id;              /* 完整JEDEC ID */
} xspi_flash_id_t;

/* =============================================================================
 * 运行统计结构体
 * ==========================================================================*/
typedef struct {
    uint32_t read_count;            /* 读取次数 */
    uint32_t write_count;           /* 写入次数 */
    uint32_t erase_sector_count;    /* 扇区擦除次数 */
    uint32_t erase_block_count;     /* 块擦除次数 */
    uint32_t erase_chip_count;      /* 整片擦除次数 */
    uint32_t err_count;             /* 错误计数 */
    uint32_t xip_enter_count;       /* XIP进入次数 */
    uint32_t xip_exit_count;        /* XIP退出次数 */
} xspi_flash_stats_t;

/* =============================================================================
 * 驱动句柄结构体
 * ==========================================================================*/
typedef struct {
    XSPI_HandleTypeDef      *hxspi;         /* XSPI HAL句柄 */
    xspi_flash_state_t      state;          /* 驱动状态 */
    xspi_flash_mode_t       mode;           /* 当前操作模式 */
    xspi_flash_id_t         flash_id;       /* Flash ID */
    xspi_flash_stats_t      stats;          /* 运行统计 */
    volatile uint8_t        busy;           /* 忙标志 */
    uint8_t                 xip_enabled;    /* XIP使能标志 */
} xspi_flash_handle_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 XSPI1 Flash 驱动
 * @param  hxspi_flash: 驱动句柄指针
 * @param  hxspi: XSPI HAL句柄指针
 * @retval XSPI_FLASH_OK 成功, 其他错误码
 */
xspi_flash_err_t xspi_flash_init(xspi_flash_handle_t *hxspi_flash, XSPI_HandleTypeDef *hxspi);

/**
 * @brief  反初始化 XSPI1 Flash 驱动
 * @param  hxspi_flash: 驱动句柄指针
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_deinit(xspi_flash_handle_t *hxspi_flash);

/**
 * @brief  读取 Flash JEDEC ID
 * @param  hxspi_flash: 驱动句柄指针
 * @param  id: 输出ID结构体
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_read_id(xspi_flash_handle_t *hxspi_flash, xspi_flash_id_t *id);

/**
 * @brief  读取数据 (支持任意长度)
 * @param  hxspi_flash: 驱动句柄指针
 * @param  addr: 起始地址 (0 ~ 64MB-1)
 * @param  buf: 输出缓冲区
 * @param  len: 读取长度
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_read(xspi_flash_handle_t *hxspi_flash, uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  快速读取数据 (OctoSPI 8-line DTR)
 * @param  hxspi_flash: 驱动句柄指针
 * @param  addr: 起始地址
 * @param  buf: 输出缓冲区
 * @param  len: 读取长度
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_fast_read(xspi_flash_handle_t *hxspi_flash, uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  写入数据 (自动分页, 自动擦除)
 * @param  hxspi_flash: 驱动句柄指针
 * @param  addr: 起始地址
 * @param  buf: 输入缓冲区
 * @param  len: 写入长度
 * @retval XSPI_FLASH_OK 成功
 * @note   自动处理跨页、跨扇区, 自动擦除
 */
xspi_flash_err_t xspi_flash_write(xspi_flash_handle_t *hxspi_flash, uint32_t addr, const uint8_t *buf, uint32_t len);

/**
 * @brief  擦除扇区 (4KB)
 * @param  hxspi_flash: 驱动句柄指针
 * @param  addr: 扇区地址 (4KB对齐)
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_erase_sector(xspi_flash_handle_t *hxspi_flash, uint32_t addr);

/**
 * @brief  擦除块 (64KB)
 * @param  hxspi_flash: 驱动句柄指针
 * @param  addr: 块地址 (64KB对齐)
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_erase_block(xspi_flash_handle_t *hxspi_flash, uint32_t addr);

/**
 * @brief  整片擦除
 * @param  hxspi_flash: 驱动句柄指针
 * @retval XSPI_FLASH_OK 成功
 * @warning 耗时约20秒
 */
xspi_flash_err_t xspi_flash_erase_chip(xspi_flash_handle_t *hxspi_flash);

/**
 * @brief  启用 XIP (Execute In Place) 内存映射模式
 * @param  hxspi_flash: 驱动句柄指针
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_enable_xip(xspi_flash_handle_t *hxspi_flash);

/**
 * @brief  禁用 XIP 模式
 * @param  hxspi_flash: 驱动句柄指针
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_disable_xip(xspi_flash_handle_t *hxspi_flash);

/**
 * @brief  获取 Flash 容量
 * @param  hxspi_flash: 驱动句柄指针
 * @retval 容量 (字节)
 */
static inline uint32_t xspi_flash_get_size(xspi_flash_handle_t *hxspi_flash)
{
    (void)hxspi_flash;
    return XSPI_FLASH_SIZE;
}

/**
 * @brief  获取驱动状态
 * @param  hxspi_flash: 驱动句柄指针
 * @retval 当前状态
 */
xspi_flash_state_t xspi_flash_get_state(xspi_flash_handle_t *hxspi_flash);

/**
 * @brief  获取运行统计
 * @param  hxspi_flash: 驱动句柄指针
 * @param  stats: 输出统计结构体
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_get_stats(xspi_flash_handle_t *hxspi_flash, xspi_flash_stats_t *stats);

/**
 * @brief  复位 Flash 设备
 * @param  hxspi_flash: 驱动句柄指针
 * @retval XSPI_FLASH_OK 成功
 */
xspi_flash_err_t xspi_flash_reset_device(xspi_flash_handle_t *hxspi_flash);

/**
 * @brief  获取错误码描述字符串
 * @param  err: 错误码
 * @retval 描述字符串
 */
const char* xspi_flash_err_to_string(xspi_flash_err_t err);

/* =============================================================================
 * 底层 HAL 回调 (弱定义, 可在应用中覆盖)
 * ==========================================================================*/

/**
 * @brief  XSPI 传输完成回调
 * @param  hxspi: XSPI HAL句柄
 */
void xspi_flash_hal_tc_callback(XSPI_HandleTypeDef *hxspi);

/**
 * @brief  XSPI 传输错误回调
 * @param  hxspi: XSPI HAL句柄
 */
void xspi_flash_hal_err_callback(XSPI_HandleTypeDef *hxspi);

#ifdef __cplusplus
}
#endif

#endif /* __XSPI_FLASH_H */
