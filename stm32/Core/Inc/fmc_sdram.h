/**
  ******************************************************************************
  * @file    fmc_sdram.h
  * @brief   FMC SDRAM 驱动头文件 (STM32N657L0H3Q) — P0批次
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - IS42S32160F: 32M x 16bit x 4banks = 256Mbit = 32MB per chip
  *   本项目使用 32-bit 数据宽度 (2片并联), 总容量 64MB
  * - 166MHz 时钟, CL=3
  * - 引脚: D0-D31(PD/PE/PF/PG), A0-A12(PF/PG), BA0-BA1(PG4/5)
  * - 控制: SDCLK(PG8), SDCKE0(PH2), SDNE0(PH3), NBL0-3
  ******************************************************************************
  */

#ifndef __FMC_SDRAM_H
#define __FMC_SDRAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
#define SDRAM_TRP                   3       /* Row Precharge Time: 15ns -> 3 clocks */
#define SDRAM_TRCD                  3       /* RAS to CAS Delay: 15ns -> 3 clocks */
#define SDRAM_TXSR                  8       /* Exit Self Refresh: 72ns -> 8 clocks */
#define SDRAM_TRRD                  2       /* Row to Row Delay: 2 clocks */

/* 刷新周期: 64ms / 8192 = 7.81us -> 约 1300 clocks @166MHz */
#define SDRAM_REFRESH_COUNT         1292

/* SDRAM 模式寄存器配置 */
#define SDRAM_MODEREG_BURST_LENGTH_1    ((uint32_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2    ((uint32_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4    ((uint32_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8    ((uint32_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQ    ((uint32_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INT    ((uint32_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2     ((uint32_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3     ((uint32_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STD  ((uint32_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROG  ((uint32_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE ((uint32_t)0x0200)

/* 超时定义 */
#define SDRAM_TIMEOUT               ((uint32_t)0xFFFF)

/* =============================================================================
 * 错误码定义
 * ==========================================================================*/
typedef enum {
    FMC_SDRAM_OK                = 0,
    FMC_SDRAM_ERR_NOT_INIT      = -1,
    FMC_SDRAM_ERR_INVALID_PARAM = -2,
    FMC_SDRAM_ERR_TIMEOUT       = -3,
    FMC_SDRAM_ERR_HARDWARE      = -4,
    FMC_SDRAM_ERR_TEST_FAIL     = -5,
} fmc_sdram_err_t;

/* =============================================================================
 * 数据结构
 * ==========================================================================*/

typedef enum {
    FMC_SDRAM_STATE_IDLE        = 0,
    FMC_SDRAM_STATE_INIT        = 1,
    FMC_SDRAM_STATE_READY       = 2,
    FMC_SDRAM_STATE_SELF_REFRESH = 3,
    FMC_SDRAM_STATE_ERROR       = 4,
} fmc_sdram_state_t;

typedef struct {
    uint32_t base_addr;
    uint32_t size;
    uint32_t width;
    uint32_t freq_hz;
    uint32_t cas_latency;
    uint32_t refresh_count;
} fmc_sdram_info_t;

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 FMC SDRAM 控制器和存储器
 * @retval FMC_SDRAM_OK 成功, 其他错误码
 */
fmc_sdram_err_t fmc_sdram_init(void);

/**
 * @brief  反初始化 FMC SDRAM
 * @retval FMC_SDRAM_OK 成功
 */
fmc_sdram_err_t fmc_sdram_deinit(void);

/**
 * @brief  SDRAM 读写测试 (memtest 风格)
 * @retval FMC_SDRAM_OK 测试通过
 */
fmc_sdram_err_t fmc_sdram_test(void);

/**
 * @brief  获取 SDRAM 基地址
 * @retval SDRAM 设备起始地址
 */
uint32_t fmc_sdram_get_base_addr(void);

/**
 * @brief  获取 SDRAM 容量
 * @retval SDRAM 容量 (字节)
 */
uint32_t fmc_sdram_get_size(void);

/**
 * @brief  获取 SDRAM 信息
 * @param  info: 信息结构体指针
 * @retval FMC_SDRAM_OK 成功
 */
fmc_sdram_err_t fmc_sdram_get_info(fmc_sdram_info_t *info);

/**
 * @brief  进入 SDRAM 自刷新模式 (低功耗)
 * @retval FMC_SDRAM_OK 成功
 */
fmc_sdram_err_t fmc_sdram_enter_self_refresh(void);

/**
 * @brief  退出 SDRAM 自刷新模式
 * @retval FMC_SDRAM_OK 成功
 */
fmc_sdram_err_t fmc_sdram_exit_self_refresh(void);

/**
 * @brief  写入 SDRAM (字访问)
 * @param  addr: 偏移地址 (相对于 SDRAM_DEVICE_ADDR)
 * @param  data: 数据指针
 * @param  len: 数据长度 (字节)
 * @retval FMC_SDRAM_OK 成功
 */
fmc_sdram_err_t fmc_sdram_write(uint32_t addr, const uint32_t *data, uint32_t len);

/**
 * @brief  读取 SDRAM (字访问)
 * @param  addr: 偏移地址 (相对于 SDRAM_DEVICE_ADDR)
 * @param  data: 数据指针
 * @param  len: 数据长度 (字节)
 * @retval FMC_SDRAM_OK 成功
 */
fmc_sdram_err_t fmc_sdram_read(uint32_t addr, uint32_t *data, uint32_t len);

/**
 * @brief  获取当前状态
 * @retval 当前状态
 */
fmc_sdram_state_t fmc_sdram_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __FMC_SDRAM_H */
