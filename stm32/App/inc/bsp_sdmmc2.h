/**
  ******************************************************************************
  * @file    bsp_sdmmc2.h
  * @brief   SDMMC2 microSD 驱动头文件 (PG9-PG14 + PD3 CD)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - SDMMC2 原生控制器，4-bit 模式
  * - 支持 SD/SDHC/SDXC (FAT32/exFAT)
  * - 用于日志存储、模型备份、配置保存
  ******************************************************************************
  */

#ifndef __BSP_SDMMC2_H
#define __BSP_SDMMC2_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * SDMMC2 配置参数
 * ==========================================================================*/
#define SDMMC2_CLK_FREQ             50000000        /* 50 MHz max (SD High Speed) */
#define SDMMC2_BLOCK_SIZE           512             /* 标准块大小 */
#define SDMMC2_TIMEOUT_MS           1000            /* 操作超时 */

/* =============================================================================
 * 数据结构
 * ==========================================================================*/
typedef struct {
    uint32_t capacity_mb;       /* 容量 (MB) */
    uint8_t  card_type;         /* 0:SDv1, 1:SDv2, 2:SDHC, 3:SDXC */
    uint32_t clock_speed;       /* 当前时钟频率 */
} sdmmc2_info_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 SDMMC2 microSD 接口
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdmmc2_init(void);

/**
 * @brief  检测 microSD 卡是否插入
 * @retval true 已插入
 */
bool bsp_sdmmc2_is_present(void);

/**
 * @brief  读取块
 * @param  block_addr: 块地址
 * @param  buf: 输出缓冲区 (512 bytes)
 * @param  count: 块数量
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdmmc2_read_blocks(uint32_t block_addr, uint8_t *buf, uint32_t count);

/**
 * @brief  写入块
 * @param  block_addr: 块地址
 * @param  buf: 输入缓冲区 (512 bytes)
 * @param  count: 块数量
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdmmc2_write_blocks(uint32_t block_addr, const uint8_t *buf, uint32_t count);

/**
 * @brief  获取 microSD 卡信息
 * @param  info: 输出信息结构
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdmmc2_get_info(sdmmc2_info_t *info);

/**
 * @brief  检查 SDMMC2 是否忙碌
 * @retval true 忙碌
 */
bool bsp_sdmmc2_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SDMMC2_H */
