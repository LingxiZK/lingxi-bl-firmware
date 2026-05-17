/**
  ******************************************************************************
  * @file    pwr_mgmt.h
  * @brief   低功耗管理 HAL 驱动头文件 (P2批次)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 睡眠模式 (Sleep Mode): CPU 停止, 外设继续运行
  * - 停止模式 (Stop Mode): CPU + HCLK 停止, 保持寄存器和 SRAM
  * - 唤醒源: RTC 闹钟/唤醒定时器、外部中断 (UWB/ToF/SD 卡检测)、SDIO 活动
  * - 功耗监控: 电流估算和统计
  ******************************************************************************
  */

#ifndef __PWR_MGMT_H
#define __PWR_MGMT_H

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
    PWR_OK              = 0,    /* 成功 */
    PWR_ERR_INIT        = -1,   /* 初始化失败 */
    PWR_ERR_MODE        = -2,   /* 模式不支持 */
    PWR_ERR_WAKEUP      = -3,   /* 唤醒源配置错误 */
    PWR_ERR_TIMEOUT     = -4,   /* 超时 */
    PWR_ERR_BUSY        = -5,   /* 系统忙, 无法进入低功耗 */
} pwr_err_t;

/* =============================================================================
 * 低功耗模式枚举
 * ==========================================================================*/
typedef enum {
    PWR_MODE_RUN        = 0,    /* 正常运行模式 (800MHz) */
    PWR_MODE_SLEEP      = 1,    /* 睡眠模式: CPU 停止, 外设继续 */
    PWR_MODE_STOP       = 2,    /* 停止模式: CPU+HCLK 停止, SRAM 保持 */
    PWR_MODE_LP_STOP    = 3,    /* 低功耗停止: 更低电压, 更长唤醒时间 */
} pwr_mode_t;

/* =============================================================================
 * 唤醒源枚举 (位掩码)
 * ==========================================================================*/
#define PWR_WAKEUP_RTC_ALARM    (1U << 0)   /* RTC 闹钟唤醒 */
#define PWR_WAKEUP_RTC_WUT      (1U << 1)   /* RTC 唤醒定时器 */
#define PWR_WAKEUP_EXTI_UWB     (1U << 2)   /* UWB IRQ 外部中断唤醒 */
#define PWR_WAKEUP_EXTI_TOF     (1U << 3)   /* ToF IRQ 外部中断唤醒 */
#define PWR_WAKEUP_EXTI_SD_CD   (1U << 4)   /* SD 卡检测唤醒 */
#define PWR_WAKEUP_SDMMC1       (1U << 5)   /* SDIO 活动唤醒 */
#define PWR_WAKEUP_UART         (1U << 6)   /* 串口唤醒 */
#define PWR_WAKEUP_ALL          0xFFFFFFFFU

/* =============================================================================
 * 功耗统计结构体
 * ==========================================================================*/
typedef struct {
    uint32_t enter_sleep_count;     /* 进入睡眠次数 */
    uint32_t enter_stop_count;      /* 进入停止次数 */
    uint32_t wakeup_count;            /* 唤醒次数 */
    uint32_t total_sleep_ms;        /* 总睡眠时间 (ms) */
    uint32_t avg_wakeup_time_us;    /* 平均唤醒时间 (us) */
    uint32_t last_sleep_duration_ms; /* 上次睡眠时长 */
    float    est_current_run_ma;      /* 运行模式估算电流 (mA) */
    float    est_current_sleep_ma;    /* 睡眠模式估算电流 (mA) */
    float    est_current_stop_ma;     /* 停止模式估算电流 (mA) */
} pwr_stats_t;

/* =============================================================================
 * 回调函数类型
 * ==========================================================================*/
typedef void (*pwr_pre_sleep_cb_t)(pwr_mode_t mode, void *user_data);     /* 进入低功耗前回调 */
typedef void (*pwr_post_wakeup_cb_t)(pwr_mode_t mode, uint32_t wakeup_src, void *user_data); /* 唤醒后回调 */

/* =============================================================================
 * 配置常量
 * ==========================================================================*/

/* RTC 唤醒定时器默认周期 */
#define PWR_RTC_WUT_DEFAULT_MS      1000    /* 默认 1 秒 */

/* 进入低功耗前的最小空闲时间 (避免频繁切换) */
#define PWR_MIN_IDLE_MS             50

/* 估算电流值 (典型值, 需根据实际测量校准) */
#define PWR_EST_I_RUN_MA            450.0f  /* 运行模式: ~450mA @ 800MHz */
#define PWR_EST_I_SLEEP_MA          120.0f  /* 睡眠模式: ~120mA */
#define PWR_EST_I_STOP_MA           15.0f   /* 停止模式: ~15mA */

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化低功耗管理模块
 * @note   配置 PWR 控制器、RTC 唤醒、EXTI 唤醒
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_Init(void);

/**
 * @brief  反初始化低功耗管理
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_DeInit(void);

/**
 * @brief  进入睡眠模式
 * @note   CPU 停止, 外设继续运行, 任意中断唤醒
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_EnterSleep(void);

/**
 * @brief  进入停止模式
 * @note   CPU + HCLK 停止, 保持寄存器和 SRAM
 * @param  wakeup_sources: 允许的唤醒源位掩码
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_EnterStop(uint32_t wakeup_sources);

/**
 * @brief  配置 RTC 唤醒定时器
 * @param  period_ms: 唤醒周期 (ms), 0 = 禁用
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_ConfigRTCWakeup(uint32_t period_ms);

/**
 * @brief  配置外部中断唤醒
 * @param  exti_pin: EXTI 引脚号 (0-15)
 * @param  trigger: 触发模式 (上升沿/下降沿/双边沿)
 * @param  enable: 1=启用, 0=禁用
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_ConfigEXTIWakeup(uint8_t exti_pin, uint8_t trigger, uint8_t enable);

/**
 * @brief  获取当前功耗模式
 * @retval 当前模式
 */
pwr_mode_t PWR_GetCurrentMode(void);

/**
 * @brief  获取唤醒源
 * @retval 唤醒源位掩码
 */
uint32_t PWR_GetWakeupSource(void);

/**
 * @brief  清除唤醒标志
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_ClearWakeupFlags(void);

/**
 * @brief  注册低功耗回调
 * @param  pre_sleep: 进入低功耗前回调 (可为 NULL)
 * @param  post_wakeup: 唤醒后回调 (可为 NULL)
 * @param  user_data: 用户数据
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_RegisterCallback(pwr_pre_sleep_cb_t pre_sleep,
                               pwr_post_wakeup_cb_t post_wakeup,
                               void *user_data);

/**
 * @brief  获取功耗统计信息
 * @param  stats: 统计结构体指针
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_GetStats(pwr_stats_t *stats);

/**
 * @brief  重置功耗统计
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_ResetStats(void);

/**
 * @brief  估算当前功耗 (mA)
 * @retval 估算电流值 (mA)
 */
float PWR_EstimateCurrent(void);

/**
 * @brief  设置系统是否允许进入低功耗
 * @param  allow: true=允许, false=禁止
 * @retval PWR_OK 成功
 */
pwr_err_t PWR_SetLowPowerAllowed(bool allow);

/**
 * @brief  检查系统是否可以进入低功耗
 * @retval true 可以进入, false 不能
 */
bool PWR_CanEnterLowPower(void);

/* =============================================================================
 * 中断服务程序声明
 * ==========================================================================*/

/**
 * @brief  RTC 唤醒中断处理
 */
void PWR_RTC_Wakeup_IRQHandler(void);

/**
 * @brief  PWR 中断处理 (唤醒标志)
 */
void PWR_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __PWR_MGMT_H */
