/**
  ******************************************************************************
  * @file    bsp_sdram.h
  * @brief   FMC SDRAM 驱动头文件 (IS42S32160F, 32-bit, 166MHz)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - IS42S32160F: 32M x 16bit x 4banks = 256Mbit = 32MB per chip
  *   本项目使用 32-bit 数据宽度 (2片并联), 总容量 64MB
  * - 166MHz 时钟, CL=3
  ******************************************************************************
  */

#ifndef __BSP_SDRAM_H
#define __BSP_SDRAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * SDRAM 配置参数 (IS42S32160F)
 * ==========================================================================*/
#define SDRAM_DEVICE_ADDR           0xC0000000U     /* FMC SDRAM Bank1 起始地址 */
#define SDRAM_DEVICE_SIZE           (64U * 1024U * 1024U)  /* 64MB */

/* 时序参数 (166MHz, 6ns clock period) */
#define SDRAM_TMRD                  2       /* Load Mode Register to Active: 2 clocks */
#define SDRAM_TRAS                  7       /* Row Active Time: 42ns -> 7 clocks */
#define SDRAM_TRC                   10      /* Row Cycle Time: 60ns -> 10 clocks */
#define SDRAM_TWR                   2       /* Write Recovery Time: 2 clocks */
#define SDRAM_TRP                   2       /* Row Precharge Time: 15ns -> 2 clocks (min 3) */
#define SDRAM_TRCD                  2       /* RAS to CAS Delay: 15ns -> 2 clocks (min 3) */
#define SDRAM_TXSR                  8       /* Exit Self Refresh: 72ns -> 8 clocks */
#define SDRAM_TRRD                  2       /* Row to Row Delay: 2 clocks */
#define SDRAM_TMRD_VALUE            2       /* Mode Register delay */

/* 刷新周期: 64ms / 8192 = 7.81us -> 约 1300 clocks @166MHz */
#define SDRAM_REFRESH_COUNT         1292

/* =============================================================================
 * API 函数声明
  * ==========================================================================*/

/**
 * @brief  初始化 FMC SDRAM 控制器和存储器
 * @retval LINGXI_OK 成功, 其他错误码
 */
lingxi_err_t bsp_sdram_init(void);

/**
 * @brief  SDRAM 读写测试 (memtest 风格)
 * @retval LINGXI_OK 测试通过
 */
lingxi_err_t bsp_sdram_test(void);

/**
 * @brief  获取 SDRAM 基地址
 * @retval SDRAM 设备起始地址
 */
static inline uint32_t bsp_sdram_get_base_addr(void)
{
    return SDRAM_DEVICE_ADDR;
}

/**
 * @brief  获取 SDRAM 容量
 * @retval SDRAM 容量 (字节)
 */
static inline uint32_t bsp_sdram_get_size(void)
{
    return SDRAM_DEVICE_SIZE;
}

/**
 * @brief  进入 SDRAM 自刷新模式 (低功耗)
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdram_enter_self_refresh(void);

/**
 * @brief  退出 SDRAM 自刷新模式
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_sdram_exit_self_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SDRAM_H */
