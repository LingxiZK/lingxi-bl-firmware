/**
  ******************************************************************************
  * @file    bsp_tof.c
  * @brief   VL53L1X ToF 驱动实现 (I2C1)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 使用 HAL I2C 驱动
  * - 中断驱动连续测量
  * - 基于 ST VL53L1X API 封装
  ******************************************************************************
  */

#include "bsp_tof.h"

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static I2C_HandleTypeDef hi2c_tof;
static volatile uint8_t s_tof_initialized = 0;
static volatile uint8_t s_tof_running = 0;

static tof_measurement_t s_last_measurement = {0};
static tof_callback_t s_tof_callback = NULL;

static SemaphoreHandle_t s_tof_sem = NULL;
static TimerHandle_t s_tof_timer = NULL;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void tof_gpio_init(void);
static void tof_i2c_init(void);
static void tof_clock_init(void);
static lingxi_err_t tof_sensor_init(void);
static void tof_timer_callback(TimerHandle_t xTimer);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 VL53L1X
 */
lingxi_err_t bsp_tof_init(void)
{
    /* 创建信号量 */
    s_tof_sem = xSemaphoreCreateBinary();
    if (s_tof_sem == NULL) {
        return LINGXI_ERR_NO_MEM;
    }

    /* 时钟初始化 */
    tof_clock_init();

    /* GPIO 初始化 */
    tof_gpio_init();

    /* I2C 初始化 */
    tof_i2c_init();

    /* 传感器初始化 */
    lingxi_err_t err = tof_sensor_init();
    LX_RETURN_IF_ERR(err);

    /* 创建定时器 (用于连续测量触发) */
    s_tof_timer = xTimerCreate(
        "ToFTimer",
        pdMS_TO_TICKS(TOF_MEASUREMENT_PERIOD_MS),
        pdTRUE,  /* 自动重载 */
        NULL,
        tof_timer_callback
    );

    if (s_tof_timer == NULL) {
        return LINGXI_ERR_NO_MEM;
    }

    /* 配置中断 */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, IRQ_PRIO_MEDIUM, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    s_tof_initialized = 1;

    LX_DEBUG_PRINT("VL53L1X ToF initialized, 400kHz I2C");
    return LINGXI_OK;
}

/**
 * @brief  启动连续测距
 */
lingxi_err_t bsp_tof_start(void)
{
    if (!s_tof_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    if (s_tof_running) {
        return LINGXI_ERR_BUSY;
    }

    s_tof_running = 1;

    /* 启动定时器 */
    xTimerStart(s_tof_timer, 0);

    LX_DEBUG_PRINT("ToF continuous measurement started");
    return LINGXI_OK;
}

/**
 * @brief  停止连续测距
 */
lingxi_err_t bsp_tof_stop(void)
{
    s_tof_running = 0;

    /* 停止定时器 */
    xTimerStop(s_tof_timer, 0);

    LX_DEBUG_PRINT("ToF continuous measurement stopped");
    return LINGXI_OK;
}

/**
 * @brief  单次测距
 */
lingxi_err_t bsp_tof_single_shot(tof_measurement_t *meas, uint32_t timeout_ms)
{
    LX_RETURN_IF_NULL(meas);

    if (!s_tof_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    /* 触发单次测量 */
    /* VL53L1X_StartRanging(); */

    /* 等待中断 */
    if (xSemaphoreTake(s_tof_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return LINGXI_ERR_TIMEOUT;
    }

    /* 读取结果 */
    taskENTER_CRITICAL();
    memcpy(meas, &s_last_measurement, sizeof(tof_measurement_t));
    taskEXIT_CRITICAL();

    return LINGXI_OK;
}

/**
 * @brief  注册回调
 */
lingxi_err_t bsp_tof_register_callback(tof_callback_t callback)
{
    if (callback == NULL) {
        return LINGXI_ERR_INVALID_PARAM;
    }
    s_tof_callback = callback;
    return LINGXI_OK;
}

/**
 * @brief  获取最新测量
 */
lingxi_err_t bsp_tof_get_measurement(tof_measurement_t *meas)
{
    LX_RETURN_IF_NULL(meas);

    taskENTER_CRITICAL();
    memcpy(meas, &s_last_measurement, sizeof(tof_measurement_t));
    taskEXIT_CRITICAL();

    return LINGXI_OK;
}

/* =============================================================================
 * 中断与回调
 * ==========================================================================*/

/**
 * @brief  ToF 中断服务程序
 */
LX_ISR_FUNC void bsp_tof_isr(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 读取测量结果 */
    /* VL53L1X_GetRangingMeasurementData(); */

    /* 更新最新测量 */
    s_last_measurement.timestamp_ms = xTaskGetTickCount();
    s_last_measurement.valid = 1;

    /* 通知等待任务 */
    xSemaphoreGiveFromISR(s_tof_sem, &xHigherPriorityTaskWoken);

    /* 调用用户回调 */
    if (s_tof_callback != NULL) {
        s_tof_callback(&s_last_measurement);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  定时器回调 (触发测量)
 */
static void tof_timer_callback(TimerHandle_t xTimer)
{
    if (s_tof_running) {
        /* 触发下一次测量 */
        /* VL53L1X_StartRanging(); */
    }
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  ToF GPIO 初始化
 */
static void tof_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* SCL: PB8, SDA: PB9 */
    GPIO_InitStruct.Pin = TOF_I2C_SCL_PIN | TOF_I2C_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(TOF_I2C_GPIO_PORT_SCL_SDA, &GPIO_InitStruct);

    /* INT: PB10 (中断输入) */
    GPIO_InitStruct.Pin = TOF_I2C_INT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(TOF_I2C_GPIO_PORT_INT, &GPIO_InitStruct);
}

/**
 * @brief  ToF I2C 初始化
 */
static void tof_i2c_init(void)
{
    hi2c_tof.Instance = TOF_I2C;
    hi2c_tof.Init.ClockSpeed = TOF_I2C_FREQ;
    hi2c_tof.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c_tof.Init.OwnAddress1 = 0;
    hi2c_tof.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c_tof.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c_tof.Init.OwnAddress2 = 0;
    hi2c_tof.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c_tof.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    HAL_I2C_Init(&hi2c_tof);
}

/**
 * @brief  ToF 时钟初始化
 */
static void tof_clock_init(void)
{
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
}

/**
 * @brief  传感器初始化序列
 */
static lingxi_err_t tof_sensor_init(void)
{
    /* 检查设备 ID */
    uint8_t id_buf[2] = {0};
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        &hi2c_tof,
        TOF_I2C_ADDR << 1,
        VL53L1X_REG_IDENTIFICATION_MODEL_ID,
        I2C_MEMADD_SIZE_16BIT,
        id_buf, 2,
        100
    );

    if (status != HAL_OK) {
        LX_ERR_PRINT("VL53L1X ID read failed");
        return LINGXI_ERR_IO;
    }

    uint16_t model_id = ((uint16_t)id_buf[0] << 8) | id_buf[1];
    if (model_id != 0xEACC) {  /* VL53L1X 预期 ID */
        LX_ERR_PRINT("VL53L1X ID mismatch: 0x%04X", model_id);
        return LINGXI_ERR_IO;
    }

    /* 初始化传感器 */
    /* VL53L1X_SensorInit(); */
    /* VL53L1X_SetDistanceMode(Long); */
    /* VL53L1X_SetTimingBudgetInMs(20); */
    /* VL53L1X_SetInterMeasurementInMs(33); */

    LX_DEBUG_PRINT("VL53L1X sensor init OK, ID=0x%04X", model_id);
    return LINGXI_OK;
}
