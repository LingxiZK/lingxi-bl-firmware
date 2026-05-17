/**
  ******************************************************************************
  * @file    mipi_csi2.c
  * @brief   MIPI CSI-2 HAL Driver Implementation (VD55G1, STM32N657)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - Target: STM32N657L0H3Q, DCMIPP + MIPI CSI-2 PHY
  * - Sensor: VD55G1, 640x480@60fps, Single Lane
  * - Data Rate: 500 Mbps per lane
  * - Image Format: YUV422 (0x1E)
  * - Frame Buffer: SDRAM via DMA double-buffering
  * - Pin Mapping: D0P/D0N(PH4/PH5), CLKP/CLKN(PH6/PH7)
  * 
  * @decision_record
  * - DR-001: 使用 DCMIPP Pipe0 作为主捕获通道, 支持 YUV422 8-bit
  * - DR-002: DMA 双缓冲使用 Circular 模式, 通过半传输/传输完成中断切换
  * - DR-003: ISR 保持最短路径, 仅设置标志和切换缓冲区索引
  * - DR-004: 帧缓冲区必须 32-byte 对齐以支持 Cache 维护
  ******************************************************************************
  */

#include "mipi_csi2.h"

/* =============================================================================
 * 私有宏
 * ==========================================================================*/
#define MIPI_CSI2_ASSERT(cond)      do { if (!(cond)) return MIPI_CSI2_ERR_INVALID_PARAM; } while(0)
#define MIPI_CSI2_CHECK_HANDLE(h)   do { if ((h) == NULL) return MIPI_CSI2_ERR_NULL_PTR; } while(0)
#define MIPI_CSI2_CHECK_INIT(h)     do { if ((h)->state != MIPI_CSI2_STATE_INIT && \
                                             (h)->state != MIPI_CSI2_STATE_READY && \
                                             (h)->state != MIPI_CSI2_STATE_RUNNING) \
                                            return MIPI_CSI2_ERR_NOT_INIT; } while(0)

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static const char *s_err_strings[] = {
    "OK",
    "NULL pointer",
    "Invalid parameter",
    "Timeout",
    "Hardware init failed",
    "DMA error",
    "PHY error",
    "Not initialized",
    "Busy",
};

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static mipi_csi2_err_t mipi_csi2_gpio_init(void);
static mipi_csi2_err_t mipi_csi2_clock_init(void);
static mipi_csi2_err_t mipi_csi2_dcmipp_init(mipi_csi2_handle_t *hcsi);
static mipi_csi2_err_t mipi_csi2_dma_init(mipi_csi2_handle_t *hcsi);
static mipi_csi2_err_t mipi_csi2_phy_config(mipi_csi2_handle_t *hcsi);
static void mipi_csi2_update_fps_stats(mipi_csi2_handle_t *hcsi);

/* =============================================================================
 * API 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 MIPI CSI-2 HAL 驱动
 */
mipi_csi2_err_t mipi_csi2_init(mipi_csi2_handle_t *hcsi, const mipi_csi2_config_t *config)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);
    MIPI_CSI2_CHECK_HANDLE(config);

    /* 验证参数 */
    if (config->width == 0 || config->height == 0 ||
        config->lane_count == 0 || config->bitrate_mbps == 0) {
        return MIPI_CSI2_ERR_INVALID_PARAM;
    }

    /* 验证缓冲区地址 */
    for (uint8_t i = 0; i < MIPI_CSI2_BUF_COUNT; i++) {
        if (config->buffer.buf_addr[i] == 0) {
            return MIPI_CSI2_ERR_INVALID_PARAM;
        }
        /* 32-byte 对齐检查 */
        if (config->buffer.buf_addr[i] & (MIPI_CSI2_BUF_ALIGN - 1)) {
            return MIPI_CSI2_ERR_INVALID_PARAM;
        }
    }

    /* 清零句柄 */
    memset(hcsi, 0, sizeof(mipi_csi2_handle_t));

    /* 保存配置 */
    memcpy(&hcsi->config, config, sizeof(mipi_csi2_config_t));
    hcsi->config.buffer.buf_size = config->width * config->height * MIPI_CSI2_CAM_BPP;

    /* 时钟初始化 */
    mipi_csi2_err_t err = mipi_csi2_clock_init();
    if (err != MIPI_CSI2_OK) {
        hcsi->state = MIPI_CSI2_STATE_ERROR;
        return err;
    }

    /* GPIO 初始化 */
    err = mipi_csi2_gpio_init();
    if (err != MIPI_CSI2_OK) {
        hcsi->state = MIPI_CSI2_STATE_ERROR;
        return err;
    }

    /* MIPI CSI-2 PHY 配置 */
    err = mipi_csi2_phy_config(hcsi);
    if (err != MIPI_CSI2_OK) {
        hcsi->state = MIPI_CSI2_STATE_ERROR;
        return err;
    }

    /* DCMIPP 初始化 */
    err = mipi_csi2_dcmipp_init(hcsi);
    if (err != MIPI_CSI2_OK) {
        hcsi->state = MIPI_CSI2_STATE_ERROR;
        return err;
    }

    /* DMA 双缓冲初始化 */
    err = mipi_csi2_dma_init(hcsi);
    if (err != MIPI_CSI2_OK) {
        hcsi->state = MIPI_CSI2_STATE_ERROR;
        return err;
    }

    hcsi->state = MIPI_CSI2_STATE_INIT;

    return MIPI_CSI2_OK;
}

/**
 * @brief  反初始化 MIPI CSI-2 HAL 驱动
 */
mipi_csi2_err_t mipi_csi2_deinit(mipi_csi2_handle_t *hcsi)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);

    /* 停止采集 */
    if (hcsi->state == MIPI_CSI2_STATE_RUNNING) {
        mipi_csi2_stop(hcsi);
    }

    /* 禁用中断 */
    HAL_NVIC_DisableIRQ(DCMIPP_IRQn);

    /* 反初始化 DMA */
    if (hcsi->hdma != NULL) {
        HAL_DMA_Abort(hcsi->hdma);
        HAL_DMA_DeInit(hcsi->hdma);
    }

    /* 反初始化 DCMIPP */
    if (hcsi->hdcmipp != NULL) {
        HAL_DCMIPP_DeInit(hcsi->hdcmipp);
    }

    /* 关闭时钟 */
    __HAL_RCC_DCMIPP_CLK_DISABLE();

    hcsi->state = MIPI_CSI2_STATE_RESET;
    return MIPI_CSI2_OK;
}

/**
 * @brief  启动图像采集
 */
mipi_csi2_err_t mipi_csi2_start(mipi_csi2_handle_t *hcsi)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);
    MIPI_CSI2_CHECK_INIT(hcsi);

    if (hcsi->dma_busy) {
        return MIPI_CSI2_ERR_BUSY;
    }

    /* 清零统计 */
    memset(&hcsi->stats, 0, sizeof(mipi_csi2_stats_t));
    hcsi->stats.min_fps = 0xFFFFFFFF;

    hcsi->dma_busy = 1;
    hcsi->frame_ready = 0;
    hcsi->config.buffer.buf_idx = 0;

    /* 启动 DMA 双缓冲传输 */
    HAL_StatusTypeDef hal_status = HAL_DMAEx_MultiBufferStart_IT(
        hcsi->hdma,
        (uint32_t)&(hcsi->hdcmipp->Instance->P0PPR),
        hcsi->config.buffer.buf_addr[0],
        hcsi->config.buffer.buf_addr[1],
        hcsi->config.buffer.buf_size / 4
    );

    if (hal_status != HAL_OK) {
        hcsi->dma_busy = 0;
        hcsi->stats.err_count++;
        return MIPI_CSI2_ERR_DMA;
    }

    /* 启动 DCMIPP 捕获 */
    hal_status = HAL_DCMIPP_Start(hcsi->hdcmipp);
    if (hal_status != HAL_OK) {
        HAL_DMA_Abort(hcsi->hdma);
        hcsi->dma_busy = 0;
        hcsi->stats.err_count++;
        return MIPI_CSI2_ERR_HW_INIT;
    }

    hcsi->state = MIPI_CSI2_STATE_RUNNING;
    return MIPI_CSI2_OK;
}

/**
 * @brief  停止图像采集
 */
mipi_csi2_err_t mipi_csi2_stop(mipi_csi2_handle_t *hcsi)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);

    if (hcsi->state != MIPI_CSI2_STATE_RUNNING) {
        return MIPI_CSI2_OK;
    }

    /* 停止 DCMIPP */
    HAL_DCMIPP_Stop(hcsi->hdcmipp);

    /* 停止 DMA */
    HAL_DMA_Abort(hcsi->hdma);

    hcsi->dma_busy = 0;
    hcsi->frame_ready = 0;
    hcsi->state = MIPI_CSI2_STATE_INIT;

    return MIPI_CSI2_OK;
}

/**
 * @brief  获取当前可读帧缓冲区
 */
mipi_csi2_err_t mipi_csi2_get_frame(mipi_csi2_handle_t *hcsi, uint8_t **frame_buf, uint32_t *frame_size)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);
    MIPI_CSI2_CHECK_HANDLE(frame_buf);
    MIPI_CSI2_CHECK_HANDLE(frame_size);

    if (hcsi->state != MIPI_CSI2_STATE_RUNNING) {
        return MIPI_CSI2_ERR_NOT_INIT;
    }

    /* 检查是否有新帧 */
    if (!hcsi->frame_ready) {
        return MIPI_CSI2_ERR_TIMEOUT;
    }

    /* 返回上一帧缓冲区 (当前写入的是另一块) */
    uint8_t read_idx = hcsi->config.buffer.buf_idx ^ 1;
    *frame_buf = (uint8_t *)hcsi->config.buffer.buf_addr[read_idx];
    *frame_size = hcsi->config.buffer.buf_size;

    /* Cache 维护: 使无效接收缓冲区 (如果使能了 D-Cache) */
    SCB_InvalidateDCache_by_Addr((uint32_t *)*frame_buf, *frame_size);

    return MIPI_CSI2_OK;
}

/**
 * @brief  释放帧缓冲区
 */
mipi_csi2_err_t mipi_csi2_release_frame(mipi_csi2_handle_t *hcsi)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);

    /* 双缓冲机制下, 获取新帧即释放旧帧, 此处仅清除标志 */
    hcsi->frame_ready = 0;
    return MIPI_CSI2_OK;
}

/**
 * @brief  注册帧就绪回调
 */
mipi_csi2_err_t mipi_csi2_register_frame_callback(mipi_csi2_handle_t *hcsi,
                                                     mipi_csi2_frame_callback_t callback,
                                                     void *user_ctx)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);

    hcsi->frame_cb = callback;
    hcsi->user_ctx = user_ctx;
    return MIPI_CSI2_OK;
}

/**
 * @brief  注册错误回调
 */
mipi_csi2_err_t mipi_csi2_register_err_callback(mipi_csi2_handle_t *hcsi,
                                                 mipi_csi2_err_callback_t callback,
                                                 void *user_ctx)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);

    hcsi->err_cb = callback;
    hcsi->user_ctx = user_ctx;
    return MIPI_CSI2_OK;
}

/**
 * @brief  获取运行统计
 */
mipi_csi2_err_t mipi_csi2_get_stats(mipi_csi2_handle_t *hcsi, mipi_csi2_stats_t *stats)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);
    MIPI_CSI2_CHECK_HANDLE(stats);

    memcpy(stats, &hcsi->stats, sizeof(mipi_csi2_stats_t));
    return MIPI_CSI2_OK;
}

/**
 * @brief  复位 MIPI CSI-2 PHY
 */
mipi_csi2_err_t mipi_csi2_phy_reset(mipi_csi2_handle_t *hcsi)
{
    MIPI_CSI2_CHECK_HANDLE(hcsi);

    /* 复位 DCMIPP */
    __HAL_RCC_DCMIPP_FORCE_RESET();
    __HAL_RCC_DCMIPP_RELEASE_RESET();

    /* 复位 MIPI CSI-2 PHY (通过 RCC) */
    /* STM32N6 的 MIPI PHY 复位由 RCC 控制 */

    return MIPI_CSI2_OK;
}

/**
 * @brief  获取驱动状态
 */
mipi_csi2_state_t mipi_csi2_get_state(mipi_csi2_handle_t *hcsi)
{
    if (hcsi == NULL) {
        return MIPI_CSI2_STATE_RESET;
    }
    return hcsi->state;
}

/**
 * @brief  获取错误码描述字符串
 */
const char* mipi_csi2_err_to_string(mipi_csi2_err_t err)
{
    int idx = -err;
    if (idx >= 0 && idx < (int)(sizeof(s_err_strings) / sizeof(s_err_strings[0]))) {
        return s_err_strings[idx];
    }
    return "Unknown error";
}

/* =============================================================================
 * 中断处理函数
 * ==========================================================================*/

/**
 * @brief  DCMIPP 全局中断处理
 */
void mipi_csi2_irq_handler(mipi_csi2_handle_t *hcsi)
{
    if (hcsi == NULL || hcsi->hdcmipp == NULL) {
        return;
    }
    HAL_DCMIPP_IRQHandler(hcsi->hdcmipp);
}

/**
 * @brief  DMA 传输完成回调 (半传输/传输完成)
 */
void mipi_csi2_dma_tc_callback(mipi_csi2_handle_t *hcsi)
{
    if (hcsi == NULL) {
        return;
    }

    /* 切换缓冲区索引 */
    hcsi->config.buffer.buf_idx ^= 1;
    hcsi->frame_ready = 1;

    /* 更新统计 */
    hcsi->stats.frame_count++;
    mipi_csi2_update_fps_stats(hcsi);

    /* 调用用户回调 */
    if (hcsi->frame_cb != NULL) {
        uint8_t read_idx = hcsi->config.buffer.buf_idx ^ 1;
        uint8_t *frame = (uint8_t *)hcsi->config.buffer.buf_addr[read_idx];
        hcsi->frame_cb(frame, hcsi->config.buffer.buf_size, hcsi->user_ctx);
    }
}

/**
 * @brief  DMA 传输错误回调
 */
void mipi_csi2_dma_err_callback(mipi_csi2_handle_t *hcsi)
{
    if (hcsi == NULL) {
        return;
    }

    hcsi->stats.err_count++;
    hcsi->dma_busy = 0;

    if (hcsi->err_cb != NULL) {
        hcsi->err_cb(0x01, hcsi->user_ctx); /* DMA error flag */
    }
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  MIPI CSI-2 GPIO 初始化
 */
static mipi_csi2_err_t mipi_csi2_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* PH4-PH7: MIPI CSI-2 D0P/D0N/CLKP/CLKN */
    GPIO_InitStruct.Pin = MIPI_CSI2_D0P_PIN | MIPI_CSI2_D0N_PIN |
                          MIPI_CSI2_CLKP_PIN | MIPI_CSI2_CLKN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = MIPI_CSI2_GPIO_AF;

    HAL_GPIO_Init(MIPI_CSI2_GPIO_PORT, &GPIO_InitStruct);

    return MIPI_CSI2_OK;
}

/**
 * @brief  MIPI CSI-2 时钟初始化
 */
static mipi_csi2_err_t mipi_csi2_clock_init(void)
{
    /* 使能 DCMIPP 时钟 */
    __HAL_RCC_DCMIPP_CLK_ENABLE();

    /* 复位 DCMIPP */
    __HAL_RCC_DCMIPP_FORCE_RESET();
    __HAL_RCC_DCMIPP_RELEASE_RESET();

    /* 使能 MIPI CSI-2 PHY 时钟 */
    /* STM32N6 的 MIPI PHY 时钟由 RCC 的专用寄存器控制 */
    /* RCC->MIPICKSELR 等寄存器配置 */

    return MIPI_CSI2_OK;
}

/**
 * @brief  MIPI CSI-2 PHY 配置
 */
static mipi_csi2_err_t mipi_csi2_phy_config(mipi_csi2_handle_t *hcsi)
{
    (void)hcsi;

    /* STM32N6 MIPI CSI-2 PHY 配置 */
    /* 通过 RCC 和 SYSCFG 寄存器配置 PHY 时钟和数据速率 */
    /* 具体寄存器配置参考 STM32N6 Reference Manual */

    /* 配置 PHY 时钟分频以支持 500Mbps */
    /* 典型配置: PHY 时钟 = bitrate / 2 = 250MHz */

    return MIPI_CSI2_OK;
}

/**
 * @brief  DCMIPP 初始化
 */
static mipi_csi2_err_t mipi_csi2_dcmipp_init(mipi_csi2_handle_t *hcsi)
{
    DCMIPP_HandleTypeDef *hdcmipp = hcsi->hdcmipp;

    if (hdcmipp == NULL) {
        /* 使用静态实例或外部传入 */
        return MIPI_CSI2_ERR_NULL_PTR;
    }

    hdcmipp->Instance = DCMIPP;

    /* DCMIPP 主配置 */
    hdcmipp->Init.SensorBus = DCMIPP_SENSOR_BUS_CSI2;
    hdcmipp->Init.ClockLaneSpeed = hcsi->config.bitrate_mbps;

    if (hcsi->config.lane_count == 1) {
        hdcmipp->Init.DataLaneNumber = DCMIPP_CSI2_ONE_DATA_LANE;
    } else if (hcsi->config.lane_count == 2) {
        hdcmipp->Init.DataLaneNumber = DCMIPP_CSI2_TWO_DATA_LANE;
    } else {
        return MIPI_CSI2_ERR_INVALID_PARAM;
    }

    hdcmipp->Init.PixelClockDivider = DCMIPP_PCLK_DIV2;

    if (HAL_DCMIPP_Init(hdcmipp) != HAL_OK) {
        return MIPI_CSI2_ERR_HW_INIT;
    }

    /* Pipe 0 配置 */
    DCMIPP_PipeConfTypeDef PipeConf = {0};

    /* 根据数据类型配置像素大小 */
    switch (hcsi->config.format) {
        case MIPI_CSI2_FMT_YUV422_8:
            PipeConf.PixelSize = DCMIPP_PIXELSIZE_16BITS; /* YUV422 = 2 bytes/pixel */
            break;
        case MIPI_CSI2_FMT_YUV422_10:
            PipeConf.PixelSize = DCMIPP_PIXELSIZE_20BITS;
            break;
        case MIPI_CSI2_FMT_RAW8:
            PipeConf.PixelSize = DCMIPP_PIXELSIZE_8BITS;
            break;
        case MIPI_CSI2_FMT_RAW10:
            PipeConf.PixelSize = DCMIPP_PIXELSIZE_16BITS;
            break;
        default:
            return MIPI_CSI2_ERR_INVALID_PARAM;
    }

    PipeConf.HSPolarity = DCMIPP_HSPOLARITY_LOW;
    PipeConf.VSPolarity = DCMIPP_VSPOLARITY_LOW;
    PipeConf.PCKPolarity = DCMIPP_PCKPOLARITY_RISING;

    if (HAL_DCMIPP_PIPE_SetConfig(hdcmipp, DCMIPP_PIPE0, &PipeConf) != HAL_OK) {
        return MIPI_CSI2_ERR_HW_INIT;
    }

    /* 裁剪配置 */
    DCMIPP_CropConfTypeDef CropConf = {0};
    CropConf.X0 = 0;
    CropConf.Y0 = 0;
    CropConf.XSize = hcsi->config.width;
    CropConf.YSize = hcsi->config.height;

    HAL_DCMIPP_PIPE_SetCropConfig(hdcmipp, DCMIPP_PIPE0, &CropConf);

    /* 使能中断 */
    HAL_NVIC_SetPriority(DCMIPP_IRQn, 1, 0); /* 高优先级 */
    HAL_NVIC_EnableIRQ(DCMIPP_IRQn);

    return MIPI_CSI2_OK;
}

/**
 * @brief  DMA 双缓冲初始化
 */
static mipi_csi2_err_t mipi_csi2_dma_init(mipi_csi2_handle_t *hcsi)
{
    DMA_HandleTypeDef *hdma = hcsi->hdma;

    if (hdma == NULL) {
        return MIPI_CSI2_ERR_NULL_PTR;
    }

    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma->Instance = DMA2_Stream0; /* 根据实际 DMA 映射表确认 */

    hdma->Init.Request = DMA_REQUEST_DCMIPP;
    hdma->Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma->Init.PeriphInc = DMA_PINC_DISABLE;
    hdma->Init.MemInc = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma->Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma->Init.Mode = DMA_CIRCULAR;
    hdma->Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma->Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma->Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma->Init.MemBurst = DMA_MBURST_INC4;
    hdma->Init.PeriphBurst = DMA_PBURST_SINGLE;

    if (HAL_DMA_Init(hdma) != HAL_OK) {
        return MIPI_CSI2_ERR_DMA;
    }

    /* 链接 DMA 到 DCMIPP */
    __HAL_LINKDMA(hcsi->hdcmipp, DMA_Handle, *hdma);

    /* 配置 DMA 中断 */
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

    return MIPI_CSI2_OK;
}

/**
 * @brief  更新 FPS 统计
 */
static void mipi_csi2_update_fps_stats(mipi_csi2_handle_t *hcsi)
{
    uint32_t now = HAL_GetTick();

    if (hcsi->stats.last_frame_tick != 0) {
        uint32_t delta = now - hcsi->stats.last_frame_tick;
        if (delta > 0) {
            uint32_t fps = 1000 / delta;
            hcsi->stats.avg_fps = fps;

            if (fps < hcsi->stats.min_fps) {
                hcsi->stats.min_fps = fps;
            }
            if (fps > hcsi->stats.max_fps) {
                hcsi->stats.max_fps = fps;
            }
        }
    }
    hcsi->stats.last_frame_tick = now;
}
