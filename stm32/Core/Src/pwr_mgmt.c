/**
  ******************************************************************************
  * @file    pwr_mgmt.c
  * @brief   低功耗管理 HAL 驱动实现 (P2批次)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 睡眠模式 (Sleep Mode): CPU 停止, 外设继续运行
  * - 停止模式 (Stop Mode): CPU + HCLK 停止, 保持寄存器和 SRAM
  * - 唤醒源: RTC 闹钟/唤醒定时器、外部中断、SDIO 活动
  * - 功耗监控: 电流估算和统计
  ******************************************************************************
  */

#include "pwr_mgmt.h"
#include "stm32n6xx_hal.h"
#include <string.h>

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static volatile pwr_mode_t s_current_mode = PWR_MODE_RUN;
static volatile uint32_t s_wakeup_source = 0;
static volatile uint8_t s_lp_allowed = 1;
static volatile uint32_t s_last_sleep_tick = 0;

static pwr_stats_t s_pwr_stats = {
    .enter_sleep_count = 0,
    .enter_stop_count = 0,
    .wakeup_count = 0,
    .total_sleep_ms = 0,
    .avg_wakeup_time_us = 0,
    .last_sleep_duration_ms = 0,
    .est_current_run_ma = PWR_EST_I_RUN_MA,
    .est_current_sleep_ma = PWR_EST_I_SLEEP_MA,
    .est_current_stop_ma = PWR_EST_I_STOP_MA,
};

static pwr_pre_sleep_cb_t s_pre_sleep_cb = NULL;
static pwr_post_wakeup_cb_t s_post_wakeup_cb = NULL;
static void *s_callback_user_data = NULL;

static RTC_HandleTypeDef hrtc;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void pwr_rtc_init(void);
static void pwr_rtc_deinit(void);
static void pwr_exti_wakeup_init(uint32_t wakeup_sources);
static void pwr_exti_wakeup_deinit(void);
static pwr_err_t pwr_enter_sleep_internal(void);
static pwr_err_t pwr_enter_stop_internal(uint32_t wakeup_sources);
static void pwr_update_stats_on_wakeup(pwr_mode_t mode, uint32_t duration_ms);

/* =============================================================================
 * RTC 初始化
 * ==========================================================================*/

/**
 * @brief  初始化 RTC 用于唤醒定时器
 */
static void pwr_rtc_init(void)
{
    /* 使能 PWR 时钟和备份域访问 */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    /* 使能 RTC 时钟 */
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();

    /* RTC 基础配置 */
    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;   /* 32.768kHz / 128 = 256Hz */
    hrtc.Init.SynchPrediv = 255;    /* 256Hz / 256 = 1Hz */
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

    HAL_RTC_Init(&hrtc);

    /* 禁用唤醒定时器 (默认) */
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
}

/**
 * @brief  反初始化 RTC
 */
static void pwr_rtc_deinit(void)
{
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    HAL_RTC_DeInit(&hrtc);
    __HAL_RCC_RTC_DISABLE();
    HAL_PWR_DisableBkUpAccess();
}

/* =============================================================================
 * EXTI 唤醒初始化
 * ==========================================================================*/

/**
 * @brief  配置 EXTI 唤醒源
 */
static void pwr_exti_wakeup_init(uint32_t wakeup_sources)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* UWB IRQ (PA8) 唤醒 */
    if (wakeup_sources & PWR_WAKEUP_EXTI_UWB) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_8;
        GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        HAL_NVIC_SetPriority(EXTI8_IRQn, 2, 0);
        HAL_NVIC_EnableIRQ(EXTI8_IRQn);
    }

    /* ToF IRQ (PB11) 唤醒 */
    if (wakeup_sources & PWR_WAKEUP_EXTI_TOF) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_11;
        GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        HAL_NVIC_SetPriority(EXTI11_IRQn, 3, 0);
        HAL_NVIC_EnableIRQ(EXTI11_IRQn);
    }

    /* SD 卡检测 (PD3) 唤醒 */
    if (wakeup_sources & PWR_WAKEUP_EXTI_SD_CD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_3;
        GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

        HAL_NVIC_SetPriority(EXTI3_IRQn, 4, 0);
        HAL_NVIC_EnableIRQ(EXTI3_IRQn);
    }
}

/**
 * @brief  反初始化 EXTI 唤醒
 */
static void pwr_exti_wakeup_deinit(void)
{
    HAL_NVIC_DisableIRQ(EXTI8_IRQn);
    HAL_NVIC_DisableIRQ(EXTI11_IRQn);
    HAL_NVIC_DisableIRQ(EXTI3_IRQn);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_8);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_11);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_3);
}

/* =============================================================================
 * 内部低功耗进入函数
 * ==========================================================================*/

/**
 * @brief  进入睡眠模式内部实现
 */
static pwr_err_t pwr_enter_sleep_internal(void)
{
    uint32_t sleep_start;

    if (!s_lp_allowed) {
        return PWR_ERR_BUSY;
    }

    /* 回调: 进入低功耗前 */
    if (s_pre_sleep_cb != NULL) {
        s_pre_sleep_cb(PWR_MODE_SLEEP, s_callback_user_data);
    }

    /* 清除唤醒标志 */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);

    sleep_start = HAL_GetTick();
    s_current_mode = PWR_MODE_SLEEP;
    s_last_sleep_tick = sleep_start;

    /* 进入睡眠模式: CPU 停止, 等待中断唤醒 */
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);

    /* 唤醒后 */
    s_current_mode = PWR_MODE_RUN;
    uint32_t duration = HAL_GetTick() - sleep_start;

    s_pwr_stats.enter_sleep_count++;
    s_pwr_stats.wakeup_count++;
    s_pwr_stats.total_sleep_ms += duration;
    s_pwr_stats.last_sleep_duration_ms = duration;

    /* 回调: 唤醒后 */
    if (s_post_wakeup_cb != NULL) {
        s_post_wakeup_cb(PWR_MODE_SLEEP, s_wakeup_source, s_callback_user_data);
    }

    return PWR_OK;
}

/**
 * @brief  进入停止模式内部实现
 */
static pwr_err_t pwr_enter_stop_internal(uint32_t wakeup_sources)
{
    uint32_t sleep_start;

    if (!s_lp_allowed) {
        return PWR_ERR_BUSY;
    }

    /* 配置唤醒源 */
    pwr_exti_wakeup_init(wakeup_sources);

    /* 回调: 进入低功耗前 */
    if (s_pre_sleep_cb != NULL) {
        s_pre_sleep_cb(PWR_MODE_STOP, s_callback_user_data);
    }

    /* 清除所有唤醒标志 */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_STOPF);

    sleep_start = HAL_GetTick();
    s_current_mode = PWR_MODE_STOP;
    s_last_sleep_tick = sleep_start;

    /* 进入停止模式 */
    /* 保持主稳压器, Flash 停止 */
    HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* 唤醒后: 重新配置系统时钟 (从 HSI 恢复) */
    SystemClock_Config();

    s_current_mode = PWR_MODE_RUN;
    uint32_t duration = HAL_GetTick() - sleep_start;

    s_pwr_stats.enter_stop_count++;
    s_pwr_stats.wakeup_count++;
    s_pwr_stats.total_sleep_ms += duration;
    s_pwr_stats.last_sleep_duration_ms = duration;

    /* 回调: 唤醒后 */
    if (s_post_wakeup_cb != NULL) {
        s_post_wakeup_cb(PWR_MODE_STOP, s_wakeup_source, s_callback_user_data);
    }

    /* 禁用 EXTI 唤醒 (避免正常运行时意外触发) */
    pwr_exti_wakeup_deinit();

    return PWR_OK;
}

/* =============================================================================
 * 公共 API 实现
 * ==========================================================================*/

/**
 * @brief  初始化低功耗管理
 */
pwr_err_t PWR_Init(void)
{
    /* 使能 PWR 时钟 */
    __HAL_RCC_PWR_CLK_ENABLE();

    /* 初始化 RTC */
    pwr_rtc_init();

    /* 配置默认状态 */
    s_current_mode = PWR_MODE_RUN;
    s_wakeup_source = 0;
    s_lp_allowed = 1;

    /* 清除所有唤醒标志 */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_STOPF);

    return PWR_OK;
}

/**
 * @brief  反初始化低功耗管理
 */
pwr_err_t PWR_DeInit(void)
{
    pwr_rtc_deinit();
    pwr_exti_wakeup_deinit();

    __HAL_RCC_PWR_CLK_DISABLE();

    s_current_mode = PWR_MODE_RUN;
    s_lp_allowed = 1;

    return PWR_OK;
}

/**
 * @brief  进入睡眠模式
 */
pwr_err_t PWR_EnterSleep(void)
{
    return pwr_enter_sleep_internal();
}

/**
 * @brief  进入停止模式
 */
pwr_err_t PWR_EnterStop(uint32_t wakeup_sources)
{
    if (wakeup_sources == 0) {
        return PWR_ERR_WAKEUP;
    }
    return pwr_enter_stop_internal(wakeup_sources);
}

/**
 * @brief  配置 RTC 唤醒定时器
 */
pwr_err_t PWR_ConfigRTCWakeup(uint32_t period_ms)
{
    if (period_ms == 0) {
        HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
        return PWR_OK;
    }

    /* RTC 时钟 = 1Hz, 唤醒计数 = period_ms / 1000 */
    /* 最小 1 秒分辨率 */
    uint32_t wake_counter = (period_ms + 999) / 1000;
    if (wake_counter == 0) wake_counter = 1;
    if (wake_counter > 0xFFFF) wake_counter = 0xFFFF;

    /* 配置唤醒定时器 */
    if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, wake_counter, RTC_WAKEUPCLOCK_CK_SPRE_16BITS) != HAL_OK) {
        return PWR_ERR_WAKEUP;
    }

    /* 启用 RTC 中断 */
    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

    return PWR_OK;
}

/**
 * @brief  配置外部中断唤醒
 */
pwr_err_t PWR_ConfigEXTIWakeup(uint8_t exti_pin, uint8_t trigger, uint8_t enable)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_TypeDef *gpio_port = NULL;
    uint16_t gpio_pin = (1U << exti_pin);

    /* 根据引脚号确定端口 (简化映射) */
    if (exti_pin == 8) {
        gpio_port = GPIOA;  /* UWB IRQ */
    } else if (exti_pin == 11) {
        gpio_port = GPIOB;  /* ToF IRQ */
    } else if (exti_pin == 3) {
        gpio_port = GPIOD;  /* SD CD */
    } else {
        return PWR_ERR_WAKEUP;
    }

    if (enable) {
        /* 使能 GPIO 时钟 */
        if (gpio_port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
        else if (gpio_port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
        else if (gpio_port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();

        GPIO_InitStruct.Pin = gpio_pin;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

        switch (trigger) {
            case 0: GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING; break;
            case 1: GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING; break;
            case 2: GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING; break;
            default: GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING; break;
        }

        HAL_GPIO_Init(gpio_port, &GPIO_InitStruct);

        /* 配置 NVIC */
        IRQn_Type irqn = (exti_pin < 5) ? (EXTI0_IRQn + exti_pin) :
                         (exti_pin < 10) ? EXTI9_5_IRQn : EXTI15_10_IRQn;
        HAL_NVIC_SetPriority(irqn, 3, 0);
        HAL_NVIC_EnableIRQ(irqn);
    } else {
        HAL_GPIO_DeInit(gpio_port, gpio_pin);
    }

    return PWR_OK;
}

/**
 * @brief  获取当前功耗模式
 */
pwr_mode_t PWR_GetCurrentMode(void)
{
    return s_current_mode;
}

/**
 * @brief  获取唤醒源
 */
uint32_t PWR_GetWakeupSource(void)
{
    uint32_t source = 0;

    /* 检查 PWR 唤醒标志 */
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_WU)) {
        source |= PWR_WAKEUP_RTC_WUT;
    }

    /* 检查 EXTI 线 */
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_8) != RESET) {
        source |= PWR_WAKEUP_EXTI_UWB;
    }
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_11) != RESET) {
        source |= PWR_WAKEUP_EXTI_TOF;
    }
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_3) != RESET) {
        source |= PWR_WAKEUP_EXTI_SD_CD;
    }

    s_wakeup_source = source;
    return source;
}

/**
 * @brief  清除唤醒标志
 */
pwr_err_t PWR_ClearWakeupFlags(void)
{
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_STOPF);

    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_8);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_11);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);

    s_wakeup_source = 0;

    return PWR_OK;
}

/**
 * @brief  注册回调函数
 */
pwr_err_t PWR_RegisterCallback(pwr_pre_sleep_cb_t pre_sleep,
                               pwr_post_wakeup_cb_t post_wakeup,
                               void *user_data)
{
    s_pre_sleep_cb = pre_sleep;
    s_post_wakeup_cb = post_wakeup;
    s_callback_user_data = user_data;

    return PWR_OK;
}

/**
 * @brief  获取功耗统计
 */
pwr_err_t PWR_GetStats(pwr_stats_t *stats)
{
    if (stats == NULL) {
        return PWR_ERR_PARAM;
    }

    memcpy(stats, &s_pwr_stats, sizeof(pwr_stats_t));
    return PWR_OK;
}

/**
 * @brief  重置功耗统计
 */
pwr_err_t PWR_ResetStats(void)
{
    memset(&s_pwr_stats, 0, sizeof(s_pwr_stats));
    s_pwr_stats.est_current_run_ma = PWR_EST_I_RUN_MA;
    s_pwr_stats.est_current_sleep_ma = PWR_EST_I_SLEEP_MA;
    s_pwr_stats.est_current_stop_ma = PWR_EST_I_STOP_MA;

    return PWR_OK;
}

/**
 * @brief  估算当前功耗
 */
float PWR_EstimateCurrent(void)
{
    switch (s_current_mode) {
        case PWR_MODE_SLEEP:
            return PWR_EST_I_SLEEP_MA;
        case PWR_MODE_STOP:
        case PWR_MODE_LP_STOP:
            return PWR_EST_I_STOP_MA;
        case PWR_MODE_RUN:
        default:
            return PWR_EST_I_RUN_MA;
    }
}

/**
 * @brief  设置是否允许进入低功耗
 */
pwr_err_t PWR_SetLowPowerAllowed(bool allow)
{
    s_lp_allowed = allow ? 1 : 0;
    return PWR_OK;
}

/**
 * @brief  检查是否可以进入低功耗
 */
bool PWR_CanEnterLowPower(void)
{
    return (s_lp_allowed != 0) && (s_current_mode == PWR_MODE_RUN);
}

/* =============================================================================
 * 中断服务程序
 * ==========================================================================*/

/**
 * @brief  RTC 唤醒中断处理
 */
void PWR_RTC_Wakeup_IRQHandler(void)
{
    HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
    s_wakeup_source |= PWR_WAKEUP_RTC_WUT;
}

/**
 * @brief  PWR 中断处理
 */
void PWR_IRQHandler(void)
{
    /* 处理 PWR 唤醒事件 */
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_WU)) {
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    }
}
