/**
  ******************************************************************************
  * @file    bsp_mipi_csi.c
  * @brief   MIPI CSI-2 驱动实现 (VD55G1, 单Lane)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 基于 STM32N6 DCMIPP + MIPI CSI-2 外设
  * - 双缓冲 DMA 传输到 SDRAM
  * - ISR 保持最短执行时间，仅设置标志和切换缓冲区
  ******************************************************************************
  */

#include "bsp_mipi_csi.h"

/* =============================================================================
 * 私有宏与常量
 * ==========================================================================*/
#define MIPI_CSI_TIMEOUT_MS         1000
#define DCMIPP_IRQn                 DCMIPP_IRQn

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static cam_config_t s_cam_cfg;
static cam_stats_t  s_cam_stats = {0};
static volatile uint8_t s_frame_ready = 0;
static volatile uint8_t s_dma_busy = 0;

/* FreeRTOS 同步原语 */
static SemaphoreHandle_t s_frame_sem = NULL;
static SemaphoreHandle_t s_dma_mutex = NULL;

/* HAL 句柄 */
static DCMIPP_HandleTypeDef hDCMIPP;
static DMA_HandleTypeDef    hDMA_DCMI;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static lingxi_err_t mipi_csi_hw_init(const cam_config_t *config);
static lingxi_err_t dcmipp_init(const cam_config_t *config);
static lingxi_err_t dma_double_buffer_init(uint32_t addr0, uint32_t addr1, uint32_t size);
static void mipi_csi_gpio_init(void);
static void mipi_csi_clock_init(void);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 MIPI CSI-2 接口
 */
lingxi_err_t bsp_mipi_csi_init(const cam_config_t *config)
{
    lingxi_err_t err = LINGXI_OK;

    LX_RETURN_IF_NULL(config);

    /* 验证参数 */
    if (config->width != CAM_WIDTH || config->height != CAM_HEIGHT) {
        return LINGXI_ERR_INVALID_PARAM;
    }

    memcpy(&s_cam_cfg, config, sizeof(cam_config_t));

    /* 创建同步原语 */
    s_frame_sem = xSemaphoreCreateBinary();
    s_dma_mutex = xSemaphoreCreateMutex();
    if (s_frame_sem == NULL || s_dma_mutex == NULL) {
        return LINGXI_ERR_NO_MEM;
    }

    /* 时钟初始化 */
    mipi_csi_clock_init();

    /* GPIO 初始化 */
    mipi_csi_gpio_init();

    /* DCMIPP 初始化 */
    err = dcmipp_init(config);
    LX_RETURN_IF_ERR(err);

    /* DMA 双缓冲初始化 */
    err = dma_double_buffer_init(config->buffer_addr[0], config->buffer_addr[1], CAM_FRAME_SIZE);
    LX_RETURN_IF_ERR(err);

    LX_DEBUG_PRINT("MIPI CSI-2 initialized: %dx%d@%d fps, %d lane(s)",
                   config->width, config->height, config->fps, MIPI_CSI_LANE_COUNT);

    return LINGXI_OK;
}

/**
 * @brief  启动摄像头采集
 */
lingxi_err_t bsp_mipi_csi_start(void)
{
    if (s_dma_busy) {
        return LINGXI_ERR_BUSY;
    }

    s_dma_busy = 1;
    s_frame_ready = 0;
    s_cam_stats.frame_count = 0;
    s_cam_stats.drop_count = 0;
    s_cam_stats.err_count = 0;

    /* 启动 DCMIPP 捕获 */
    HAL_StatusTypeDef hal_status = HAL_DCMIPP_Start(&hDCMIPP);
    if (hal_status != HAL_OK) {
        s_dma_busy = 0;
        return LINGXI_ERR_IO;
    }

    LX_DEBUG_PRINT("MIPI CSI-2 capture started");
    return LINGXI_OK;
}

/**
 * @brief  停止摄像头采集
 */
lingxi_err_t bsp_mipi_csi_stop(void)
{
    HAL_DCMIPP_Stop(&hDCMIPP);
    s_dma_busy = 0;

    LX_DEBUG_PRINT("MIPI CSI-2 capture stopped");
    return LINGXI_OK;
}

/**
 * @brief  获取当前帧缓冲区 (阻塞式，带超时)
 */
uint8_t* bsp_mipi_csi_get_frame(void)
{
    /* 等待帧就绪信号 */
    if (xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(MIPI_CSI_TIMEOUT_MS)) != pdTRUE) {
        return NULL; /* 超时 */
    }

    /* 返回当前可读缓冲区 */
    uint8_t idx = s_cam_cfg.buffer_idx ^ 1; /* 上一帧 */
    return (uint8_t*)s_cam_cfg.buffer_addr[idx];
}

/**
 * @brief  释放帧缓冲区
 */
lingxi_err_t bsp_mipi_csi_release_frame(uint8_t *frame)
{
    LX_RETURN_IF_NULL(frame);
    /* 双缓冲机制下，获取新帧即释放旧帧，此处仅做校验 */
    return LINGXI_OK;
}

/**
 * @brief  获取统计信息
 */
lingxi_err_t bsp_mipi_csi_get_stats(cam_stats_t *stats)
{
    LX_RETURN_IF_NULL(stats);
    memcpy(stats, (void*)&s_cam_stats, sizeof(cam_stats_t));
    return LINGXI_OK;
}

/* =============================================================================
 * 中断与回调
 * ==========================================================================*/

/**
 * @brief  MIPI CSI-2 中断服务程序
 * @note   保持最短执行时间！
 */
LX_ISR_FUNC void bsp_mipi_csi_isr(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    HAL_DCMIPP_IRQHandler(&hDCMIPP);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  DCMIPP 帧中断回调 (HAL 弱定义覆盖)
 */
void HAL_DCMIPP_VsyncEventCallback(DCMIPP_HandleTypeDef *hdcmipp)
{
    s_cam_stats.frame_count++;

    /* 计算实际 FPS */
    uint32_t now = xTaskGetTickCount();
    if (s_cam_stats.last_frame_time_ms != 0) {
        uint32_t delta = now - s_cam_stats.last_frame_time_ms;
        if (delta > 0) {
            s_cam_stats.avg_fps = 1000 / delta;
        }
    }
    s_cam_stats.last_frame_time_ms = now;
}

/**
 * @brief  DCMIPP DMA 传输完成回调
 */
void HAL_DCMIPP_PipeEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe, uint32_t Event)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (Event == DCMIPP_PIPE_FRAME_EVENT) {
        /* 切换缓冲区索引 */
        s_cam_cfg.buffer_idx ^= 1;
        s_frame_ready = 1;

        /* 通知任务 */
        xSemaphoreGiveFromISR(s_frame_sem, &xHigherPriorityTaskWoken);

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief  DCMIPP 错误回调
 */
void HAL_DCMIPP_ErrorCallback(DCMIPP_HandleTypeDef *hdcmipp)
{
    s_cam_stats.err_count++;
    LX_ERR_PRINT("DCMIPP error occurred");
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  MIPI CSI-2 GPIO 初始化
 */
static void mipi_csi_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* PH4-PH7: MIPI CSI-2 D0+/D0-/CLK+/CLK- */
    GPIO_InitStruct.Pin = MIPI_CSI_D0_PIN | MIPI_CSI_D0N_PIN |
                          MIPI_CSI_CLK_PIN | MIPI_CSI_CLKN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_DCMIPP; /* 根据实际 datasheet 确认 */
    HAL_GPIO_Init(MIPI_CSI_GPIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief  MIPI CSI-2 时钟初始化
 */
static void mipi_csi_clock_init(void)
{
    /* 使能 DCMIPP 和 MIPI CSI-2 时钟 */
    __HAL_RCC_DCMIPP_CLK_ENABLE();
    __HAL_RCC_DCMIPP_FORCE_RESET();
    __HAL_RCC_DCMIPP_RELEASE_RESET();

    /* MIPI CSI-2 PHY 时钟配置 */
    /* 根据 STM32N6 Reference Manual 配置 RCC */
    /* RCC->MIPICKSELR 等寄存器 */
}

/**
 * @brief  DCMIPP 初始化
 */
static lingxi_err_t dcmipp_init(const cam_config_t *config)
{
    hDCMIPP.Instance = DCMIPP;

    /* DCMIPP 主配置 */
    hDCMIPP.Init.SensorBus = DCMIPP_SENSOR_BUS_CSI2;
    hDCMIPP.Init.ClockLaneSpeed = MIPI_CSI_CLK_MBPS;
    hDCMIPP.Init.DataLaneNumber = DCMIPP_CSI2_ONE_DATA_LANE;
    hDCMIPP.Init.PixelClockDivider = DCMIPP_PCLK_DIV2;

    if (HAL_DCMIPP_Init(&hDCMIPP) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* Pipe 0 配置 (主捕获通道) */
    DCMIPP_PipeConfTypeDef PipeConf = {0};
    PipeConf.PixelSize = DCMIPP_PIXELSIZE_16BITS; /* RAW10 packed */

    if (config->format == CAM_FMT_RAW8) {
        PipeConf.PixelSize = DCMIPP_PIXELSIZE_8BITS;
    } else if (config->format == CAM_FMT_RAW10) {
        PipeConf.PixelSize = DCMIPP_PIXELSIZE_16BITS; /* 10-bit in 16-bit container */
    }

    PipeConf.HSPolarity = DCMIPP_HSPOLARITY_LOW;
    PipeConf.VSPolarity = DCMIPP_VSPOLARITY_LOW;
    PipeConf.PCKPolarity = DCMIPP_PCKPOLARITY_RISING;

    if (HAL_DCMIPP_PIPE_SetConfig(&hDCMIPP, DCMIPP_PIPE0, &PipeConf) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* 裁剪配置 (如需) */
    DCMIPP_CropConfTypeDef CropConf = {0};
    CropConf.X0 = 0;
    CropConf.Y0 = 0;
    CropConf.XSize = config->width;
    CropConf.YSize = config->height;

    HAL_DCMIPP_PIPE_SetCropConfig(&hDCMIPP, DCMIPP_PIPE0, &CropConf);

    /* 使能中断 */
    HAL_NVIC_SetPriority(DCMIPP_IRQn, IRQ_PRIO_CRITICAL, 0);
    HAL_NVIC_EnableIRQ(DCMIPP_IRQn);

    return LINGXI_OK;
}

/**
 * @brief  DMA 双缓冲初始化
 */
static lingxi_err_t dma_double_buffer_init(uint32_t addr0, uint32_t addr1, uint32_t size)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hDMA_DCMI.Instance = DMA2_Stream0; /* 根据实际 DMA 映射 */

    hDMA_DCMI.Init.Request = DMA_REQUEST_DCMIPP;
    hDMA_DCMI.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hDMA_DCMI.Init.PeriphInc = DMA_PINC_DISABLE;
    hDMA_DCMI.Init.MemInc = DMA_MINC_ENABLE;
    hDMA_DCMI.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hDMA_DCMI.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hDMA_DCMI.Init.Mode = DMA_CIRCULAR; /* 循环模式实现双缓冲 */
    hDMA_DCMI.Init.Priority = DMA_PRIORITY_VERY_HIGH;

    if (HAL_DMA_Init(&hDMA_DCMI) != HAL_OK) {
        return LINGXI_ERR_DMA;
    }

    /* 配置双缓冲模式 */
    HAL_DMAEx_MultiBufferStart(&hDMA_DCMI,
                               (uint32_t)&DCMIPP->P0PPR, /* DCMIPP Pipe0 数据寄存器 */
                               addr0,
                               addr1,
                               size / 4); /* 以 word 为单位 */

    return LINGXI_OK;
}
