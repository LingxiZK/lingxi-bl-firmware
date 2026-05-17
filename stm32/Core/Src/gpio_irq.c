/**
  ******************************************************************************
  * @file    gpio_irq.c
  * @brief   GPIO 中断 HAL 驱动实现 (P2批次)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - DWM3000 UWB_IRQ (PA8) 外部中断
  * - VL53L1X GPIO1 (PB11) 外部中断
  * - SYNC_OUT (PA0/PA1) TIM2 PWM 输出
  * - 中断优先级分层配置
  ******************************************************************************
  */

#include "gpio_irq.h"
#include "stm32n6xx_hal.h"
#include <string.h>

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static TIM_HandleTypeDef hsync_tim;

/* 中断状态表 */
typedef struct {
    volatile uint8_t      enabled;       /* 启用标志 */
    volatile uint32_t     irq_count;     /* 中断计数 */
    gpio_irq_trigger_t    trigger;       /* 触发模式 */
    gpio_irq_callback_t   callback;      /* 回调函数 */
    void                 *user_data;       /* 用户数据 */
    GPIO_TypeDef         *gpio_port;     /* GPIO 端口 */
    uint16_t              gpio_pin;      /* GPIO 引脚 */
    uint32_t              exti_line;     /* EXTI 线 */
    IRQn_Type             irqn;          /* NVIC 中断号 */
    uint32_t              priority;      /* 中断优先级 */
} gpio_irq_state_t;

static gpio_irq_state_t s_irq_states[GPIO_IRQ_MAX] = {
    [GPIO_IRQ_UWB] = {
        .enabled    = 0,
        .irq_count  = 0,
        .trigger    = GPIO_IRQ_RISING,
        .callback   = NULL,
        .user_data  = NULL,
        .gpio_port  = UWB_IRQ_GPIO_PORT,
        .gpio_pin   = UWB_IRQ_GPIO_PIN,
        .exti_line  = UWB_IRQ_EXTI_LINE,
        .irqn       = UWB_IRQ_EXTI_IRQn,
        .priority   = IRQ_PRIO_UWB,
    },
    [GPIO_IRQ_TOF] = {
        .enabled    = 0,
        .irq_count  = 0,
        .trigger    = GPIO_IRQ_FALLING,
        .callback   = NULL,
        .user_data  = NULL,
        .gpio_port  = TOF_IRQ_GPIO_PORT,
        .gpio_pin   = TOF_IRQ_GPIO_PIN,
        .exti_line  = TOF_IRQ_EXTI_LINE,
        .irqn       = TOF_IRQ_EXTI_IRQn,
        .priority   = IRQ_PRIO_TOF,
    },
    [GPIO_IRQ_SYNC0] = {
        .enabled    = 0,
        .irq_count  = 0,
        .trigger    = GPIO_IRQ_RISING,
        .callback   = NULL,
        .user_data  = NULL,
        .gpio_port  = SYNC0_GPIO_PORT,
        .gpio_pin   = SYNC0_GPIO_PIN,
        .exti_line  = 0,  /* TIM 非 EXTI */
        .irqn       = SYNC_TIM_IRQn,
        .priority   = IRQ_PRIO_SYNC,
    },
    [GPIO_IRQ_SYNC1] = {
        .enabled    = 0,
        .irq_count  = 0,
        .trigger    = GPIO_IRQ_RISING,
        .callback   = NULL,
        .user_data  = NULL,
        .gpio_port  = SYNC1_GPIO_PORT,
        .gpio_pin   = SYNC1_GPIO_PIN,
        .exti_line  = 0,
        .irqn       = SYNC_TIM_IRQn,
        .priority   = IRQ_PRIO_SYNC,
    },
};

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void gpio_irq_exti_init(gpio_irq_source_t src);
static void gpio_irq_exti_deinit(gpio_irq_source_t src);
static void gpio_irq_tim_init(void);
static void gpio_irq_tim_deinit(void);
static void gpio_irq_dispatch(gpio_irq_source_t src);

/* =============================================================================
 * EXTI GPIO 中断初始化
 * ==========================================================================*/

/**
 * @brief  初始化指定 EXTI 中断源
 */
static void gpio_irq_exti_init(gpio_irq_source_t src)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    gpio_irq_state_t *state = &s_irq_states[src];

    /* 使能 GPIO 时钟 */
    if (state->gpio_port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (state->gpio_port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }

    /* GPIO 配置为输入 + 外部中断 */
    GPIO_InitStruct.Pin = state->gpio_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;  /* 默认, 后续根据 trigger 调整 */
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(state->gpio_port, &GPIO_InitStruct);

    /* 配置 EXTI */
    HAL_NVIC_SetPriority(state->irqn, state->priority, 0);
}

/**
 * @brief  反初始化指定 EXTI 中断源
 */
static void gpio_irq_exti_deinit(gpio_irq_source_t src)
{
    gpio_irq_state_t *state = &s_irq_states[src];

    HAL_GPIO_DeInit(state->gpio_port, state->gpio_pin);
    HAL_NVIC_DisableIRQ(state->irqn);

    state->enabled = 0;
    state->callback = NULL;
}

/* =============================================================================
 * TIM2 SYNC 输出初始化
 * ==========================================================================*/

/**
 * @brief  初始化 TIM2 用于 SYNC_OUT PWM
 */
static void gpio_irq_tim_init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint32_t tim_clk;

    /* 使能时钟 */
    SYNC_TIM_CLK_EN();
    SYNC_GPIO_CLK_EN();

    /* 计算 TIM2 时钟 (APB1 = 200MHz, TIM2 倍频 = 400MHz) */
    tim_clk = 400000000U;

    /* TIM2 基础配置: 1MHz 计数频率 */
    hsync_tim.Instance = SYNC_TIM_INSTANCE;
    hsync_tim.Init.Prescaler = (tim_clk / 1000000U) - 1;  /* 1MHz */
    hsync_tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    hsync_tim.Init.Period = 999;  /* 默认 1kHz (1MHz / 1000) */
    hsync_tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    hsync_tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&hsync_tim);

    /* PWM 模式配置 */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 500;  /* 50% 默认占空比 */
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    /* 通道1: PA0 */
    HAL_TIM_PWM_ConfigChannel(&hsync_tim, &sConfigOC, TIM_CHANNEL_1);

    /* 通道2: PA1 */
    sConfigOC.Pulse = 500;
    HAL_TIM_PWM_ConfigChannel(&hsync_tim, &sConfigOC, TIM_CHANNEL_2);

    /* GPIO 配置为复用推挽 */
    GPIO_InitStruct.Pin = SYNC0_GPIO_PIN | SYNC1_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(SYNC0_GPIO_PORT, &GPIO_InitStruct);

    /* TIM2 中断配置 (用于软件同步触发) */
    HAL_NVIC_SetPriority(SYNC_TIM_IRQn, IRQ_PRIO_SYNC, 0);
}

/**
 * @brief  反初始化 TIM2
 */
static void gpio_irq_tim_deinit(void)
{
    HAL_TIM_PWM_DeInit(&hsync_tim);
    HAL_GPIO_DeInit(SYNC0_GPIO_PORT, SYNC0_GPIO_PIN | SYNC1_GPIO_PIN);
    HAL_NVIC_DisableIRQ(SYNC_TIM_IRQn);
    __HAL_RCC_TIM2_CLK_DISABLE();
}

/* =============================================================================
 * 中断分发
 * ==========================================================================*/

/**
 * @brief  中断分发处理
 */
static void gpio_irq_dispatch(gpio_irq_source_t src)
{
    gpio_irq_state_t *state = &s_irq_states[src];

    if (!state->enabled) {
        return;
    }

    state->irq_count++;

    if (state->callback != NULL) {
        state->callback(src, HAL_GetTick(), state->user_data);
    }
}

/* =============================================================================
 * 公共 API 实现
 * ==========================================================================*/

/**
 * @brief  初始化所有 GPIO 中断和 SYNC 输出
 */
gpio_irq_err_t GPIO_IRQ_Init(void)
{
    /* 初始化 EXTI 中断 */
    gpio_irq_exti_init(GPIO_IRQ_UWB);
    gpio_irq_exti_init(GPIO_IRQ_TOF);

    /* 初始化 TIM2 SYNC */
    gpio_irq_tim_init();

    return GPIO_IRQ_OK;
}

/**
 * @brief  反初始化所有 GPIO 中断
 */
gpio_irq_err_t GPIO_IRQ_DeInit(void)
{
    gpio_irq_exti_deinit(GPIO_IRQ_UWB);
    gpio_irq_exti_deinit(GPIO_IRQ_TOF);
    gpio_irq_tim_deinit();

    return GPIO_IRQ_OK;
}

/**
 * @brief  配置指定中断源
 */
gpio_irq_err_t GPIO_IRQ_Config(gpio_irq_source_t src,
                               gpio_irq_trigger_t trigger,
                               gpio_irq_callback_t callback,
                               void *user_data)
{
    if (src >= GPIO_IRQ_MAX) {
        return GPIO_IRQ_ERR_PARAM;
    }

    gpio_irq_state_t *state = &s_irq_states[src];

    /* 先禁用 */
    GPIO_IRQ_Disable(src);

    state->trigger = trigger;
    state->callback = callback;
    state->user_data = user_data;

    /* 重新配置 GPIO 模式 */
    if (src == GPIO_IRQ_UWB || src == GPIO_IRQ_TOF) {
        GPIO_InitTypeDef GPIO_InitStruct = {0};

        GPIO_InitStruct.Pin = state->gpio_pin;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

        switch (trigger) {
            case GPIO_IRQ_RISING:
                GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
                break;
            case GPIO_IRQ_FALLING:
                GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
                break;
            case GPIO_IRQ_BOTH:
                GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
                break;
            default:
                GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
                break;
        }

        HAL_GPIO_Init(state->gpio_port, &GPIO_InitStruct);
    }

    return GPIO_IRQ_OK;
}

/**
 * @brief  启用指定中断源
 */
gpio_irq_err_t GPIO_IRQ_Enable(gpio_irq_source_t src)
{
    if (src >= GPIO_IRQ_MAX) {
        return GPIO_IRQ_ERR_PARAM;
    }

    gpio_irq_state_t *state = &s_irq_states[src];

    if (src == GPIO_IRQ_UWB || src == GPIO_IRQ_TOF) {
        HAL_NVIC_EnableIRQ(state->irqn);
    }

    state->enabled = 1;
    return GPIO_IRQ_OK;
}

/**
 * @brief  禁用指定中断源
 */
gpio_irq_err_t GPIO_IRQ_Disable(gpio_irq_source_t src)
{
    if (src >= GPIO_IRQ_MAX) {
        return GPIO_IRQ_ERR_PARAM;
    }

    gpio_irq_state_t *state = &s_irq_states[src];

    if (src == GPIO_IRQ_UWB || src == GPIO_IRQ_TOF) {
        HAL_NVIC_DisableIRQ(state->irqn);
    }

    state->enabled = 0;
    return GPIO_IRQ_OK;
}

/**
 * @brief  获取中断源当前电平状态
 */
GPIO_PinState GPIO_IRQ_GetPinState(gpio_irq_source_t src)
{
    if (src >= GPIO_IRQ_MAX) {
        return GPIO_PIN_RESET;
    }

    return HAL_GPIO_ReadPin(s_irq_states[src].gpio_port, s_irq_states[src].gpio_pin);
}

/**
 * @brief  配置 SYNC_OUT PWM
 */
gpio_irq_err_t GPIO_IRQ_ConfigSyncPWM(uint32_t freq_hz, uint8_t duty0, uint8_t duty1)
{
    if (freq_hz == 0 || freq_hz > 500000) {
        return GPIO_IRQ_ERR_PARAM;
    }

    if (duty0 > 100) duty0 = 100;
    if (duty1 > 100) duty1 = 100;

    /* 停止当前输出 */
    HAL_TIM_PWM_Stop(&hsync_tim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&hsync_tim, TIM_CHANNEL_2);

    /* 重新计算周期 */
    uint32_t period = (1000000U / freq_hz) - 1;
    if (period > 65535) period = 65535;

    __HAL_TIM_SET_AUTORELOAD(&hsync_tim, period);

    /* 更新占空比 */
    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    /* 通道1 */
    sConfigOC.Pulse = (period * duty0) / 100;
    HAL_TIM_PWM_ConfigChannel(&hsync_tim, &sConfigOC, TIM_CHANNEL_1);

    /* 通道2 */
    sConfigOC.Pulse = (period * duty1) / 100;
    HAL_TIM_PWM_ConfigChannel(&hsync_tim, &sConfigOC, TIM_CHANNEL_2);

    return GPIO_IRQ_OK;
}

/**
 * @brief  启动 SYNC_OUT
 */
gpio_irq_err_t GPIO_IRQ_StartSync(void)
{
    HAL_TIM_PWM_Start(&hsync_tim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&hsync_tim, TIM_CHANNEL_2);

    s_irq_states[GPIO_IRQ_SYNC0].enabled = 1;
    s_irq_states[GPIO_IRQ_SYNC1].enabled = 1;

    return GPIO_IRQ_OK;
}

/**
 * @brief  停止 SYNC_OUT
 */
gpio_irq_err_t GPIO_IRQ_StopSync(void)
{
    HAL_TIM_PWM_Stop(&hsync_tim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&hsync_tim, TIM_CHANNEL_2);

    s_irq_states[GPIO_IRQ_SYNC0].enabled = 0;
    s_irq_states[GPIO_IRQ_SYNC1].enabled = 0;

    return GPIO_IRQ_OK;
}

/**
 * @brief  获取中断统计
 */
gpio_irq_err_t GPIO_IRQ_GetStats(gpio_irq_source_t src, uint32_t *count)
{
    if (src >= GPIO_IRQ_MAX || count == NULL) {
        return GPIO_IRQ_ERR_PARAM;
    }

    *count = s_irq_states[src].irq_count;
    return GPIO_IRQ_OK;
}

/* =============================================================================
 * 中断服务程序
 * ==========================================================================*/

/**
 * @brief  UWB IRQ 中断处理 (EXTI8)
 */
void GPIO_IRQ_UWB_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(UWB_IRQ_GPIO_PIN) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(UWB_IRQ_GPIO_PIN);
        gpio_irq_dispatch(GPIO_IRQ_UWB);
    }
}

/**
 * @brief  ToF IRQ 中断处理 (EXTI11)
 */
void GPIO_IRQ_TOF_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(TOF_IRQ_GPIO_PIN) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(TOF_IRQ_GPIO_PIN);
        gpio_irq_dispatch(GPIO_IRQ_TOF);
    }
}

/**
 * @brief  TIM2 中断处理 (SYNC_OUT)
 */
void GPIO_IRQ_TIM2_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&hsync_tim, TIM_FLAG_UPDATE) != RESET) {
        if (__HAL_TIM_GET_IT_SOURCE(&hsync_tim, TIM_IT_UPDATE) != RESET) {
            __HAL_TIM_CLEAR_IT(&hsync_tim, TIM_IT_UPDATE);
            /* 可在此添加软件同步触发逻辑 */
        }
    }

    /* 处理通道比较中断 */
    if (__HAL_TIM_GET_FLAG(&hsync_tim, TIM_FLAG_CC1) != RESET) {
        __HAL_TIM_CLEAR_IT(&hsync_tim, TIM_IT_CC1);
    }
    if (__HAL_TIM_GET_FLAG(&hsync_tim, TIM_FLAG_CC2) != RESET) {
        __HAL_TIM_CLEAR_IT(&hsync_tim, TIM_IT_CC2);
    }
}
