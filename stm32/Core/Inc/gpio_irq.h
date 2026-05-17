/**
  ******************************************************************************
  * @file    gpio_irq.h
  * @brief   GPIO 中断 HAL 驱动头文件 (P2批次)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - DWM3000 UWB_IRQ (PA8) 外部中断
  * - VL53L1X GPIO1 (PB11) 外部中断
  * - SYNC_OUT (PA0/PA1) TIM2 输出
  * - 中断优先级分层配置
  ******************************************************************************
  */

#ifndef __GPIO_IRQ_H
#define __GPIO_IRQ_H

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
    GPIO_IRQ_OK             = 0,    /* 成功 */
    GPIO_IRQ_ERR_INIT       = -1,   /* 初始化失败 */
    GPIO_IRQ_ERR_PARAM      = -2,   /* 参数错误 */
    GPIO_IRQ_ERR_NOT_FOUND  = -3,   /* 中断源未找到 */
    GPIO_IRQ_ERR_BUSY       = -4,   /* 忙 */
} gpio_irq_err_t;

/* =============================================================================
 * 中断源枚举
 * ==========================================================================*/
typedef enum {
    GPIO_IRQ_UWB        = 0,    /* DWM3000 UWB_IRQ (PA8) */
    GPIO_IRQ_TOF        = 1,    /* VL53L1X GPIO1 (PB11) */
    GPIO_IRQ_SYNC0      = 2,    /* SYNC_OUT0 (PA0) TIM2_CH1 */
    GPIO_IRQ_SYNC1      = 3,    /* SYNC_OUT1 (PA1) TIM2_CH2 */
    GPIO_IRQ_MAX        = 4,
} gpio_irq_source_t;

/* =============================================================================
 * 中断触发模式
 * ==========================================================================*/
typedef enum {
    GPIO_IRQ_RISING     = 0,    /* 上升沿触发 */
    GPIO_IRQ_FALLING    = 1,    /* 下降沿触发 */
    GPIO_IRQ_BOTH       = 2,    /* 双边沿触发 */
    GPIO_IRQ_LEVEL_HIGH = 3,    /* 高电平 */
    GPIO_IRQ_LEVEL_LOW  = 4,    /* 低电平 */
} gpio_irq_trigger_t;

/* =============================================================================
 * 回调函数类型
 * ==========================================================================*/
typedef void (*gpio_irq_callback_t)(gpio_irq_source_t src, uint32_t tick_ms, void *user_data);

/* =============================================================================
 * GPIO 引脚定义 (与 lingxi_bl.h 保持一致)
 * ==========================================================================*/

/* UWB IRQ: PA8 */
#define UWB_IRQ_GPIO_PORT       GPIOA
#define UWB_IRQ_GPIO_PIN        GPIO_PIN_8
#define UWB_IRQ_GPIO_CLK_EN()   __HAL_RCC_GPIOA_CLK_ENABLE()
#define UWB_IRQ_EXTI_LINE       EXTI_LINE_8
#define UWB_IRQ_EXTI_IRQn       EXTI8_IRQn

/* ToF IRQ: PB11 */
#define TOF_IRQ_GPIO_PORT       GPIOB
#define TOF_IRQ_GPIO_PIN        GPIO_PIN_11
#define TOF_IRQ_GPIO_CLK_EN()   __HAL_RCC_GPIOB_CLK_ENABLE()
#define TOF_IRQ_EXTI_LINE       EXTI_LINE_11
#define TOF_IRQ_EXTI_IRQn       EXTI11_IRQn

/* SYNC_OUT: PA0/PA1 (TIM2 通道) */
#define SYNC0_GPIO_PORT         GPIOA
#define SYNC0_GPIO_PIN          GPIO_PIN_0
#define SYNC1_GPIO_PORT         GPIOA
#define SYNC1_GPIO_PIN          GPIO_PIN_1
#define SYNC_GPIO_CLK_EN()      __HAL_RCC_GPIOA_CLK_ENABLE()
#define SYNC_TIM_INSTANCE       TIM2
#define SYNC_TIM_CLK_EN()       __HAL_RCC_TIM2_CLK_ENABLE()
#define SYNC_TIM_IRQn           TIM2_IRQn

/* 中断优先级 (NVIC Priority Group 4) */
#define IRQ_PRIO_UWB            2       /* 高优先级: UWB 测距 */
#define IRQ_PRIO_TOF            3       /* 中优先级: ToF 测量 */
#define IRQ_PRIO_SYNC           4       /* 低优先级: 同步输出 */

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化所有 GPIO 中断和 SYNC 输出
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_Init(void);

/**
 * @brief  反初始化所有 GPIO 中断
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_DeInit(void);

/**
 * @brief  配置指定中断源
 * @param  src: 中断源
 * @param  trigger: 触发模式
 * @param  callback: 回调函数 (NULL = 禁用回调)
 * @param  user_data: 用户数据
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_Config(gpio_irq_source_t src,
                               gpio_irq_trigger_t trigger,
                               gpio_irq_callback_t callback,
                               void *user_data);

/**
 * @brief  启用指定中断源
 * @param  src: 中断源
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_Enable(gpio_irq_source_t src);

/**
 * @brief  禁用指定中断源
 * @param  src: 中断源
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_Disable(gpio_irq_source_t src);

/**
 * @brief  获取中断源当前电平状态
 * @param  src: 中断源
 * @retval GPIO_PIN_SET 或 GPIO_PIN_RESET
 */
GPIO_PinState GPIO_IRQ_GetPinState(gpio_irq_source_t src);

/**
 * @brief  配置 SYNC_OUT PWM 输出
 * @param  freq_hz: 频率 (Hz)
 * @param  duty0: 通道0 占空比 (0-100)
 * @param  duty1: 通道1 占空比 (0-100)
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_ConfigSyncPWM(uint32_t freq_hz, uint8_t duty0, uint8_t duty1);

/**
 * @brief  启动 SYNC_OUT 输出
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_StartSync(void);

/**
 * @brief  停止 SYNC_OUT 输出
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_StopSync(void);

/**
 * @brief  获取中断统计信息
 * @param  src: 中断源
 * @param  count: 中断计数输出
 * @retval GPIO_IRQ_OK 成功
 */
gpio_irq_err_t GPIO_IRQ_GetStats(gpio_irq_source_t src, uint32_t *count);

/* =============================================================================
 * 中断服务程序声明 (在 stm32n6xx_it.c 中调用)
 * ==========================================================================*/

/**
 * @brief  UWB IRQ 中断处理 (EXTI8)
 */
void GPIO_IRQ_UWB_IRQHandler(void);

/**
 * @brief  ToF IRQ 中断处理 (EXTI11)
 */
void GPIO_IRQ_TOF_IRQHandler(void);

/**
 * @brief  TIM2 中断处理 (SYNC_OUT)
 */
void GPIO_IRQ_TIM2_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __GPIO_IRQ_H */
