/**
  ******************************************************************************
  * @file    spi2_sdcard.h
  * @brief   SPI2 microSD HAL 驱动头文件 (P2批次)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 基于 STM32N6xx HAL 库
  * - SPI2 @ 25MHz max, 软件 CS 控制
  * - 支持 SD/SDHC/SDXC (FAT32), 配合 FatFs 使用
  * - 引脚: PB13(SCK), PB14(MISO), PB15(MOSI), PB12(CS), PD3(CD)
  ******************************************************************************
  */

#ifndef __SPI2_SDCARD_H
#define __SPI2_SDCARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * 错误码定义
 * ==========================================================================*/
typedef enum {
    SD_OK               = 0,    /* 成功 */
    SD_ERR_INIT         = -1,   /* 初始化失败 */
    SD_ERR_CMD          = -2,   /* 命令错误 */
    SD_ERR_TIMEOUT      = -3,   /* 超时 */
    SD_ERR_CRC          = -4,   /* CRC 错误 */
    SD_ERR_NOT_PRESENT  = -5,   /* 卡未插入 */
    SD_ERR_WRITE_PROT   = -6,   /* 写保护 */
    SD_ERR_IO           = -7,   /* IO 错误 */
    SD_ERR_DMA          = -8,   /* DMA 错误 */
} sd_err_t;

/* =============================================================================
 * SD 卡类型
 * ==========================================================================*/
typedef enum {
    SD_TYPE_NONE    = 0,    /* 未检测到 */
    SD_TYPE_MMC     = 1,    /* MMC */
    SD_TYPE_V1      = 2,    /* SD v1.x */
    SD_TYPE_V2      = 3,    /* SD v2.0 */
    SD_TYPE_V2HC    = 4,    /* SD v2.0 HC (SDHC) */
    SD_TYPE_V2XC    = 5,    /* SD v2.0 XC (SDXC) */
} sd_type_t;

/* =============================================================================
 * SD 卡信息结构体
 * ==========================================================================*/
typedef struct {
    sd_type_t   type;           /* 卡类型 */
    uint32_t    capacity_mb;    /* 容量 (MB) */
    uint32_t    block_count;    /* 块数量 */
    uint32_t    block_size;     /* 块大小 (通常为 512) */
    uint8_t     ocr[4];         /* OCR 寄存器 */
    uint8_t     cid[16];        /* CID 寄存器 */
    uint8_t     csd[16];        /* CSD 寄存器 */
    uint8_t     write_protect;  /* 写保护标志 */
} sd_info_t;

/* =============================================================================
 * FatFs 磁盘 I/O 接口 (diskio 层)
 * ==========================================================================*/
/* FatFs 驱动号 */
#define SD_FATFS_DRIVE_NUM      0

/* =============================================================================
 * 配置参数
 * ==========================================================================*/
#define SD_SPI_BAUDRATE_INIT    400000      /* 初始化波特率 400kHz */
#define SD_SPI_BAUDRATE_MAX     25000000    /* 最大波特率 25MHz */
#define SD_BLOCK_SIZE           512         /* 标准块大小 */
#define SD_TIMEOUT_MS           1000        /* 操作超时 (ms) */
#define SD_INIT_RETRIES         5           /* 初始化重试次数 */
#define SD_DMA_BUFFER_ALIGN     32          /* DMA 缓冲区对齐 (cache line) */

/* =============================================================================
 * GPIO 引脚定义 (与 lingxi_bl.h 保持一致)
 * ==========================================================================*/
#define SD_SPI                  SPI2
#define SD_SPI_IRQn             SPI2_IRQn
#define SD_SPI_CLK_ENABLE()     __HAL_RCC_SPI2_CLK_ENABLE()
#define SD_SPI_CLK_DISABLE()    __HAL_RCC_SPI2_CLK_DISABLE()
#define SD_SPI_FORCE_RESET()    __HAL_RCC_SPI2_FORCE_RESET()
#define SD_SPI_RELEASE_RESET()  __HAL_RCC_SPI2_RELEASE_RESET()

#define SD_GPIO_PORT_SPI        GPIOB
#define SD_GPIO_PORT_CD         GPIOD
#define SD_GPIO_CLK_ENABLE()    do { __HAL_RCC_GPIOB_CLK_ENABLE(); \
                                       __HAL_RCC_GPIOD_CLK_ENABLE(); } while(0)

#define SD_PIN_SCK              GPIO_PIN_13     /* PB13: SPI2_SCK */
#define SD_PIN_MISO             GPIO_PIN_14     /* PB14: SPI2_MISO */
#define SD_PIN_MOSI             GPIO_PIN_15     /* PB15: SPI2_MOSI */
#define SD_PIN_CS               GPIO_PIN_12     /* PB12: SPI2_NSS (软件控制) */
#define SD_PIN_CD               GPIO_PIN_3      /* PD3: SD_CD (卡检测) */

#define SD_AF_SPI               GPIO_AF5_SPI2

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 SPI2 SD 卡接口
 * @retval SD_OK 成功, 其他错误码
 */
sd_err_t SD_Init(void);

/**
 * @brief  反初始化 SPI2 SD 接口
 * @retval SD_OK 成功
 */
sd_err_t SD_DeInit(void);

/**
 * @brief  检测 SD 卡是否插入
 * @retval true 已插入, false 未插入
 */
bool SD_IsPresent(void);

/**
 * @brief  获取 SD 卡信息
 * @param  info: 信息结构体指针
 * @retval SD_OK 成功
 */
sd_err_t SD_GetInfo(sd_info_t *info);

/**
 * @brief  读取单个块 (512 bytes)
 * @param  block_addr: 块地址 (SDHC/SDXC 为块地址, SD 为字节地址需转换)
 * @param  buf: 输出缓冲区 (512 bytes, DMA 对齐)
 * @retval SD_OK 成功
 */
sd_err_t SD_ReadBlock(uint32_t block_addr, uint8_t *buf);

/**
 * @brief  写入单个块 (512 bytes)
 * @param  block_addr: 块地址
 * @param  buf: 输入缓冲区 (512 bytes, DMA 对齐)
 * @retval SD_OK 成功
 */
sd_err_t SD_WriteBlock(uint32_t block_addr, const uint8_t *buf);

/**
 * @brief  读取多个连续块
 * @param  block_addr: 起始块地址
 * @param  buf: 输出缓冲区
 * @param  num_blocks: 块数量
 * @retval SD_OK 成功
 */
sd_err_t SD_ReadBlocks(uint32_t block_addr, uint8_t *buf, uint32_t num_blocks);

/**
 * @brief  写入多个连续块
 * @param  block_addr: 起始块地址
 * @param  buf: 输入缓冲区
 * @param  num_blocks: 块数量
 * @retval SD_OK 成功
 */
sd_err_t SD_WriteBlocks(uint32_t block_addr, const uint8_t *buf, uint32_t num_blocks);

/**
 * @brief  获取 SD 卡状态
 * @retval SD_OK 就绪, SD_ERR_BUSY 忙
 */
sd_err_t SD_GetStatus(void);

/**
 * @brief  获取卡检测 GPIO 状态 (原始电平)
 * @retval GPIO_PIN_RESET 已插入 (低电平), GPIO_PIN_SET 未插入
 */
GPIO_PinState SD_GetCardDetectPinState(void);

/* =============================================================================
 * FatFs 磁盘 I/O 接口函数 (供 diskio.c 调用)
 * ==========================================================================*/

/**
 * @brief  FatFs 磁盘初始化
 * @param  pdrv: 物理驱动号
 * @retval DSTATUS (RES_OK=0, STA_NOINIT=0x01, STA_NODISK=0x02, STA_PROTECT=0x04)
 */
DSTATUS SD_disk_initialize(BYTE pdrv);

/**
 * @brief  FatFs 磁盘状态查询
 * @param  pdrv: 物理驱动号
 * @retval DSTATUS
 */
DSTATUS SD_disk_status(BYTE pdrv);

/**
 * @brief  FatFs 磁盘读取
 * @param  pdrv: 物理驱动号
 * @param  buff: 数据缓冲区
 * @param  sector: 起始扇区号
 * @param  count: 扇区数量
 * @retval DRESULT (RES_OK=0, RES_ERROR=1, RES_WRPRT=2, RES_NOTRDY=3, RES_PARERR=4)
 */
DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);

/**
 * @brief  FatFs 磁盘写入
 * @param  pdrv: 物理驱动号
 * @param  buff: 数据缓冲区
 * @param  sector: 起始扇区号
 * @param  count: 扇区数量
 * @retval DRESULT
 */
DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);

/**
 * @brief  FatFs 磁盘控制 (IOCTL)
 * @param  pdrv: 物理驱动号
 * @param  cmd: 控制命令
 * @param  buff: 参数/数据缓冲区
 * @retval DRESULT
 */
DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#ifdef __cplusplus
}
#endif

#endif /* __SPI2_SDCARD_H */
