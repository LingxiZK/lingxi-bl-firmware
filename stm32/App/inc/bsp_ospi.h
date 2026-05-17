/**
  ******************************************************************************
  * @file    bsp_ospi.h
  * @brief   OctoSPI Flash 驱动头文件 (W25Q512, 8线)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - W25Q512JV: 128Mbit = 64MB, OctoSPI 8线模式
  * - 支持 XIP (Execute In Place) 模式
  * - 用于存储 NPU 模型、固件 OTA 备份
  ******************************************************************************
  */

#ifndef __BSP_OSPI_H
#define __BSP_OSPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * W25Q512 配置参数
 * ==========================================================================*/
#define OSPI_FLASH_SIZE             (16U * 1024U * 1024U)   /* 64MB */
#define OSPI_SECTOR_SIZE            (4U * 1024U)            /* 4KB sector */
#define OSPI_BLOCK_SIZE             (64U * 1024U)           /* 64KB block */
#define OSPI_PAGE_SIZE              256                     /* 256 bytes/page */
#define OSPI_SECTOR_COUNT           (OSPI_FLASH_SIZE / OSPI_SECTOR_SIZE)

/* W25Q512 指令 */
#define W25Q_CMD_RDID               0x9F    /* Read JEDEC ID */
#define W25Q_CMD_WREN               0x06    /* Write Enable */
#define W25Q_CMD_WRDI               0x04    /* Write Disable */
#define W25Q_CMD_RDSR1              0x05    /* Read Status Register 1 */
#define W25Q_CMD_RDSR2              0x35    /* Read Status Register 2 */
#define W25Q_CMD_RDSR3              0x15    /* Read Status Register 3 */
#define W25Q_CMD_WRSR               0x01    /* Write Status Register */
#define W25Q_CMD_PP                 0x02    /* Page Program */
#define W25Q_CMD_SE                 0x20    /* Sector Erase (4KB) */
#define W25Q_CMD_BE                 0xD8    /* Block Erase (64KB) */
#define W25Q_CMD_CE                 0xC7    /* Chip Erase */
#define W25Q_CMD_READ               0x03    /* Read Data */
#define W25Q_CMD_FAST_READ          0x0B    /* Fast Read */
#define W25Q_CMD_4READ              0x6B    /* Quad Output Fast Read */
#define W25Q_CMD_8READ              0xEB    /* Octal Output Fast Read */
#define W25Q_CMD_8PP                0xC0    /* Octal Page Program */

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 OctoSPI 接口和 Flash
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_init(void);

/**
 * @brief  读取 Flash ID
 * @param  id: 输出 ID (3 bytes)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_read_id(uint32_t *id);

/**
 * @brief  读取数据
 * @param  addr: 起始地址
 * @param  buf: 输出缓冲区
 * @param  len: 读取长度
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  写入数据 (自动分页)
 * @param  addr: 起始地址
 * @param  buf: 输入缓冲区
 * @param  len: 写入长度
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_write(uint32_t addr, const uint8_t *buf, uint32_t len);

/**
 * @brief  擦除扇区 (4KB)
 * @param  addr: 扇区地址 (4KB 对齐)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_erase_sector(uint32_t addr);

/**
 * @brief  擦除块 (64KB)
 * @param  addr: 块地址 (64KB 对齐)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_erase_block(uint32_t addr);

/**
 * @brief  整片擦除 (耗时较长)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_erase_chip(void);

/**
 * @brief  启用 XIP 模式 (Execute In Place)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_enable_xip(void);

/**
 * @brief  禁用 XIP 模式
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_ospi_disable_xip(void);

/**
 * @brief  获取 Flash 容量
 * @retval 容量 (字节)
 */
static inline uint32_t bsp_ospi_get_size(void)
{
    return OSPI_FLASH_SIZE;
}

#ifdef __cplusplus
}
#endif

#endif /* __BSP_OSPI_H */
