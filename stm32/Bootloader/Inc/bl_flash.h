/*******************************************************************************
 * @file    bl_flash.h
 * @brief   Bootloader Flash 操作封装 — OctoSPI XIP (W25Q512)
 * @version 1.1.0
 *
 * 修正说明 (v1.1.0):
 *   STM32N657 没有用户可写的 Internal Flash。
 *   所有非易失操作(包括 Info Sector)都通过 XSPI 操作 W25Q512。
 *   OTA 接收缓存使用 RAM(而非 Flash)。
 ******************************************************************************/

#ifndef BL_FLASH_H
#define BL_FLASH_H

#include <stdint.h>
#include <stdbool.h>
#include "bl_config.h"
#include "stm32n6xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 初始化与控制
 *===========================================================================*/
bl_err_t bl_flash_init(void);
void     bl_flash_deinit(void);

/*============================================================================
 * OctoSPI XIP Flash 操作 (W25Q512)
 *===========================================================================*/
bl_err_t bl_xspi_init(void);
bl_err_t bl_xspi_erase_sector(uint32_t addr);      /* 4KB sector */
bl_err_t bl_xspi_erase_block64k(uint32_t addr);    /* 64KB block */
bl_err_t bl_xspi_write_page(uint32_t addr, const uint8_t *data, uint16_t len);
bl_err_t bl_xspi_read(uint32_t addr, uint8_t *buf, uint32_t len);
bl_err_t bl_xspi_wait_busy(uint32_t timeout_ms);

/*============================================================================
 * Info Sector 管理 (通过 XSPI 操作 W25Q512)
 *===========================================================================*/
bl_err_t bl_info_read(bl_info_sector_t *info);
bl_err_t bl_info_write(const bl_info_sector_t *info);
bl_err_t bl_info_init_default(void);
bool     bl_info_validate(const bl_info_sector_t *info);
uint32_t bl_info_calc_crc(const bl_info_sector_t *info);

/*============================================================================
 * 固件校验
 *===========================================================================*/
uint32_t bl_crc32(const uint8_t *data, uint32_t len);
bl_err_t bl_verify_firmware(uint32_t addr, uint32_t size, uint32_t expected_crc);

/*============================================================================
 * 跳转 API
 *===========================================================================*/
void bl_jump_to_app(uint32_t xip_addr);

#ifdef __cplusplus
}
#endif

#endif /* BL_FLASH_H */
