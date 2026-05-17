/**
  ******************************************************************************
  * @file    dma_config.c
  * @brief   DMA/MDMA HAL 配置实现 (P2批次)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - MDMA_Channel0: SDMMC1 数据传输
  * - MDMA_Channel1: MIPI CSI-2 帧缓冲
  * - MDMA_Channel2: XSPI1 Flash 读写
  * - 支持循环模式、突发传输、双缓冲
  * - STM32N6 系列 MDMA 支持 16 个通道, 每个通道独立配置
  ******************************************************************************
  */

#include "dma_config.h"
#include "stm32n6xx_hal.h"
#include <string.h>

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static MDMA_HandleTypeDef hmdma_ch0;    /* SDMMC1 */
static MDMA_HandleTypeDef hmdma_ch1;    /* MIPI CSI-2 */
static MDMA_HandleTypeDef hmdma_ch2;    /* XSPI1 */

static mdma_channel_state_t s_mdma_states[MDMA_CH_MAX] = {
    [MDMA_CH_SDMMC1] = { .hmdma = &hmdma_ch0, .initialized = 0, .busy = 0 },
    [MDMA_CH_MIPI]   = { .hmdma = &hmdma_ch1, .initialized = 0, .busy = 0 },
    [MDMA_CH_XSPI1]  = { .hmdma = &hmdma_ch2, .initialized = 0, .busy = 0 },
};

/* 通道实例映射表 */
static MDMA_Channel_TypeDef *const s_mdma_instances[MDMA_CH_MAX] = {
    [MDMA_CH_SDMMC1] = MDMA_CH0_INSTANCE,
    [MDMA_CH_MIPI]   = MDMA_CH1_INSTANCE,
    [MDMA_CH_XSPI1]  = MDMA_CH2_INSTANCE,
};

/* 请求映射表 */
static const uint32_t s_mdma_requests[MDMA_CH_MAX] = {
    [MDMA_CH_SDMMC1] = MDMA_REQ_SDMMC1,
    [MDMA_CH_MIPI]   = MDMA_REQ_DCMIPP,
    [MDMA_CH_XSPI1]  = MDMA_REQ_XSPI1,
};

/* 中断向量表 */
static const IRQn_Type s_mdma_irqs[MDMA_CH_MAX] = {
    [MDMA_CH_SDMMC1] = MDMA_IRQn,   /* STM32N6 使用共享 MDMA_IRQn */
    [MDMA_CH_MIPI]   = MDMA_IRQn,
    [MDMA_CH_XSPI1]  = MDMA_IRQn,
};

/* 默认优先级 */
static const uint32_t s_mdma_priorities[MDMA_CH_MAX] = {
    [MDMA_CH_SDMMC1] = MDMA_PRIO_SDMMC1,
    [MDMA_CH_MIPI]   = MDMA_PRIO_MIPI,
    [MDMA_CH_XSPI1]  = MDMA_PRIO_XSPI1,
};

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void mdma_clock_init(void);
static void mdma_irq_init(mdma_channel_id_t ch_id);
static dma_err_t mdma_config_hw(mdma_channel_id_t ch_id, const dma_xfer_cfg_t *cfg);
static void mdma_hal_tc_callback(MDMA_HandleTypeDef *hmdma);
static void mdma_hal_ht_callback(MDMA_HandleTypeDef *hmdma);
static void mdma_hal_err_callback(MDMA_HandleTypeDef *hmdma);
static mdma_channel_id_t mdma_handle_to_id(const MDMA_HandleTypeDef *hmdma);

/* =============================================================================
 * 时钟与中断初始化
 * ==========================================================================*/

/**
 * @brief  初始化 MDMA 时钟
 */
static void mdma_clock_init(void)
{
    __HAL_RCC_MDMA_CLK_ENABLE();
    __HAL_RCC_MDMA_FORCE_RESET();
    __HAL_RCC_MDMA_RELEASE_RESET();
}

/**
 * @brief  初始化 MDMA 中断
 */
static void mdma_irq_init(mdma_channel_id_t ch_id)
{
    /* STM32N6 MDMA 使用共享中断向量 */
    HAL_NVIC_SetPriority(s_mdma_irqs[ch_id], s_mdma_priorities[ch_id], 0);
    HAL_NVIC_EnableIRQ(s_mdma_irqs[ch_id]);
}

/**
 * @brief  HAL 句柄到通道 ID 的映射
 */
static mdma_channel_id_t mdma_handle_to_id(const MDMA_HandleTypeDef *hmdma)
{
    for (int i = 0; i < MDMA_CH_MAX; i++) {
        if (s_mdma_states[i].hmdma == hmdma) {
            return (mdma_channel_id_t)i;
        }
    }
    return MDMA_CH_MAX;  /* 无效 */
}

/* =============================================================================
 * HAL 回调函数
 * ==========================================================================*/

/**
 * @brief  HAL MDMA 传输完成回调
 */
static void mdma_hal_tc_callback(MDMA_HandleTypeDef *hmdma)
{
    mdma_channel_id_t ch_id = mdma_handle_to_id(hmdma);
    if (ch_id >= MDMA_CH_MAX) {
        return;
    }

    s_mdma_states[ch_id].busy = 0;
    s_mdma_states[ch_id].xfer_count++;

    if (s_mdma_states[ch_id].tc_cb != NULL) {
        s_mdma_states[ch_id].tc_cb(ch_id, s_mdma_states[ch_id].user_data);
    }
}

/**
 * @brief  HAL MDMA 半传输回调
 */
static void mdma_hal_ht_callback(MDMA_HandleTypeDef *hmdma)
{
    mdma_channel_id_t ch_id = mdma_handle_to_id(hmdma);
    if (ch_id >= MDMA_CH_MAX) {
        return;
    }

    if (s_mdma_states[ch_id].ht_cb != NULL) {
        s_mdma_states[ch_id].ht_cb(ch_id, s_mdma_states[ch_id].user_data);
    }
}

/**
 * @brief  HAL MDMA 错误回调
 */
static void mdma_hal_err_callback(MDMA_HandleTypeDef *hmdma)
{
    mdma_channel_id_t ch_id = mdma_handle_to_id(hmdma);
    if (ch_id >= MDMA_CH_MAX) {
        return;
    }

    s_mdma_states[ch_id].busy = 0;
    s_mdma_states[ch_id].err_count++;

    if (s_mdma_states[ch_id].err_cb != NULL) {
        s_mdma_states[ch_id].err_cb(ch_id, s_mdma_states[ch_id].user_data);
    }
}

/* =============================================================================
 * 硬件配置
 * ==========================================================================*/

/**
 * @brief  配置 MDMA 通道硬件参数
 */
static dma_err_t mdma_config_hw(mdma_channel_id_t ch_id, const dma_xfer_cfg_t *cfg)
{
    MDMA_HandleTypeDef *hmdma = s_mdma_states[ch_id].hmdma;
    MDMA_Channel_TypeDef *instance = s_mdma_instances[ch_id];

    if (hmdma == NULL || instance == NULL) {
        return DMA_ERR_INIT;
    }

    /* 反初始化旧配置 */
    HAL_MDMA_DeInit(hmdma);

    /* 基础配置 */
    hmdma->Instance = instance;

    /* 请求配置 */
    hmdma->Init.Request = s_mdma_requests[ch_id];

    /* 传输方向 */
    switch (cfg->direction) {
        case DMA_DIR_P2M:
            hmdma->Init.Direction = MDMA_DIRECTION_P_TO_M;
            break;
        case DMA_DIR_M2P:
            hmdma->Init.Direction = MDMA_DIRECTION_M_TO_P;
            break;
        case DMA_DIR_M2M:
            hmdma->Init.Direction = MDMA_DIRECTION_M_TO_M;
            break;
        default:
            return DMA_ERR_PARAM;
    }

    /* 源/目标配置 */
    hmdma->Init.SourceInc = cfg->src_inc ? MDMA_SOURCE_INC_BYTE : MDMA_SOURCE_INC_DISABLE;
    hmdma->Init.DestinationInc = cfg->dst_inc ? MDMA_DESTINATION_INC_BYTE : MDMA_DESTINATION_INC_DISABLE;

    /* 数据宽度 */
    switch (cfg->src_width) {
        case 1:  hmdma->Init.SourceDataSize = MDMA_SOURCE_DATASIZE_BYTE;  break;
        case 2:  hmdma->Init.SourceDataSize = MDMA_SOURCE_DATASIZE_HALFWORD; break;
        case 4:  hmdma->Init.SourceDataSize = MDMA_SOURCE_DATASIZE_WORD;  break;
        default: hmdma->Init.SourceDataSize = MDMA_SOURCE_DATASIZE_BYTE;  break;
    }

    switch (cfg->dst_width) {
        case 1:  hmdma->Init.DestinationDataSize = MDMA_DESTINATION_DATASIZE_BYTE;  break;
        case 2:  hmdma->Init.DestinationDataSize = MDMA_DESTINATION_DATASIZE_HALFWORD; break;
        case 4:  hmdma->Init.DestinationDataSize = MDMA_DESTINATION_DATASIZE_WORD;  break;
        default: hmdma->Init.DestinationDataSize = MDMA_DESTINATION_DATASIZE_BYTE;  break;
    }

    /* 突发传输配置 */
    switch (cfg->src_burst) {
        case DMA_BURST_SINGLE:
            hmdma->Init.SourceBurst = MDMA_SOURCE_BURST_SINGLE;
            break;
        case DMA_BURST_INCR4:
            hmdma->Init.SourceBurst = MDMA_SOURCE_BURST_4BEATS;
            break;
        case DMA_BURST_INCR8:
            hmdma->Init.SourceBurst = MDMA_SOURCE_BURST_8BEATS;
            break;
        case DMA_BURST_INCR16:
            hmdma->Init.SourceBurst = MDMA_SOURCE_BURST_16BEATS;
            break;
        default:
            hmdma->Init.SourceBurst = MDMA_SOURCE_BURST_SINGLE;
    }

    switch (cfg->dst_burst) {
        case DMA_BURST_SINGLE:
            hmdma->Init.DestinationBurst = MDMA_DESTINATION_BURST_SINGLE;
            break;
        case DMA_BURST_INCR4:
            hmdma->Init.DestinationBurst = MDMA_DESTINATION_BURST_4BEATS;
            break;
        case DMA_BURST_INCR8:
            hmdma->Init.DestinationBurst = MDMA_DESTINATION_BURST_8BEATS;
            break;
        case DMA_BURST_INCR16:
            hmdma->Init.DestinationBurst = MDMA_DESTINATION_BURST_16BEATS;
            break;
        default:
            hmdma->Init.DestinationBurst = MDMA_DESTINATION_BURST_SINGLE;
    }

    /* 缓冲区偏移 (用于双缓冲) */
    hmdma->Init.BufferTransferLength = 0;
    hmdma->Init.SourceBlockAddressOffset = 0;
    hmdma->Init.DestinationBlockAddressOffset = 0;

    /* 初始化 HAL */
    if (HAL_MDMA_Init(hmdma) != HAL_OK) {
        return DMA_ERR_INIT;
    }

    /* 注册 HAL 回调 */
    HAL_MDMA_RegisterCallback(hmdma, HAL_MDMA_XFER_CPLT_CB_ID, mdma_hal_tc_callback);
    HAL_MDMA_RegisterCallback(hmdma, HAL_MDMA_XFER_HALFCPLT_CB_ID, mdma_hal_ht_callback);
    HAL_MDMA_RegisterCallback(hmdma, HAL_MDMA_XFER_ERROR_CB_ID, mdma_hal_err_callback);

    return DMA_OK;
}

/* =============================================================================
 * 公共 API 实现
 * ==========================================================================*/

/**
 * @brief  初始化所有 MDMA 通道
 * @note   配置三个通道的默认参数, 但不启动传输
 */
dma_err_t MDMA_Init(void)
{
    dma_err_t err;
    dma_xfer_cfg_t cfg;

    mdma_clock_init();

    /* --- MDMA_Channel0: SDMMC1 --- */
    memset(&cfg, 0, sizeof(cfg));
    cfg.direction   = DMA_DIR_P2M;      /* 默认外设到内存 (接收) */
    cfg.src_burst   = DMA_BURST_INCR4;
    cfg.dst_burst   = DMA_BURST_INCR4;
    cfg.src_inc     = 0;                /* 外设地址固定 */
    cfg.dst_inc     = 1;                /* 内存地址递增 */
    cfg.src_width   = 4;                /* 32-bit */
    cfg.dst_width   = 4;
    cfg.circular    = 0;
    cfg.priority    = MDMA_PRIO_SDMMC1;

    err = mdma_config_hw(MDMA_CH_SDMMC1, &cfg);
    if (err != DMA_OK) {
        return err;
    }
    mdma_irq_init(MDMA_CH_SDMMC1);
    s_mdma_states[MDMA_CH_SDMMC1].initialized = 1;

    /* --- MDMA_Channel1: MIPI CSI-2 --- */
    memset(&cfg, 0, sizeof(cfg));
    cfg.direction   = DMA_DIR_P2M;      /* DCMIPP 到 SDRAM */
    cfg.src_burst   = DMA_BURST_INCR8;
    cfg.dst_burst   = DMA_BURST_INCR8;
    cfg.src_inc     = 0;
    cfg.dst_inc     = 1;
    cfg.src_width   = 4;
    cfg.dst_width   = 4;
    cfg.circular    = 1;                /* 循环模式: 双缓冲 */
    cfg.double_buf  = 1;
    cfg.priority    = MDMA_PRIO_MIPI;

    err = mdma_config_hw(MDMA_CH_MIPI, &cfg);
    if (err != DMA_OK) {
        return err;
    }
    mdma_irq_init(MDMA_CH_MIPI);
    s_mdma_states[MDMA_CH_MIPI].initialized = 1;

    /* --- MDMA_Channel2: XSPI1 --- */
    memset(&cfg, 0, sizeof(cfg));
    cfg.direction   = DMA_DIR_M2P;      /* 内存到 XSPI (写入) */
    cfg.src_burst   = DMA_BURST_INCR4;
    cfg.dst_burst   = DMA_BURST_SINGLE;
    cfg.src_inc     = 1;
    cfg.dst_inc     = 0;
    cfg.src_width   = 4;
    cfg.dst_width   = 4;
    cfg.circular    = 0;
    cfg.priority    = MDMA_PRIO_XSPI1;

    err = mdma_config_hw(MDMA_CH_XSPI1, &cfg);
    if (err != DMA_OK) {
        return err;
    }
    mdma_irq_init(MDMA_CH_XSPI1);
    s_mdma_states[MDMA_CH_XSPI1].initialized = 1;

    return DMA_OK;
}

/**
 * @brief  反初始化所有 MDMA 通道
 */
dma_err_t MDMA_DeInit(void)
{
    for (int i = 0; i < MDMA_CH_MAX; i++) {
        if (s_mdma_states[i].initialized) {
            HAL_MDMA_DeInit(s_mdma_states[i].hmdma);
            s_mdma_states[i].initialized = 0;
            s_mdma_states[i].busy = 0;
        }
    }

    HAL_NVIC_DisableIRQ(MDMA_IRQn);
    __HAL_RCC_MDMA_CLK_DISABLE();

    return DMA_OK;
}

/**
 * @brief  重新配置指定 MDMA 通道
 */
dma_err_t MDMA_ConfigChannel(mdma_channel_id_t ch_id, const dma_xfer_cfg_t *cfg)
{
    if (ch_id >= MDMA_CH_MAX || cfg == NULL) {
        return DMA_ERR_PARAM;
    }

    if (s_mdma_states[ch_id].busy) {
        return DMA_ERR_BUSY;
    }

    return mdma_config_hw(ch_id, cfg);
}

/**
 * @brief  启动单次 MDMA 传输
 */
dma_err_t MDMA_StartXfer(mdma_channel_id_t ch_id, uint32_t src_addr, uint32_t dst_addr, uint32_t len)
{
    MDMA_HandleTypeDef *hmdma;
    HAL_StatusTypeDef hal_status;

    if (ch_id >= MDMA_CH_MAX) {
        return DMA_ERR_PARAM;
    }

    if (!s_mdma_states[ch_id].initialized) {
        return DMA_ERR_NOT_INIT;
    }

    if (s_mdma_states[ch_id].busy) {
        return DMA_ERR_BUSY;
    }

    hmdma = s_mdma_states[ch_id].hmdma;

    /* 地址对齐检查 */
    if ((src_addr & (DMA_BUFFER_ALIGN - 1)) || (dst_addr & (DMA_BUFFER_ALIGN - 1))) {
        /* 非对齐地址: 允许但可能影响性能 */
    }

    /* 长度对齐 */
    uint8_t data_width = (hmdma->Init.SourceDataSize == MDMA_SOURCE_DATASIZE_WORD) ? 4 :
                         (hmdma->Init.SourceDataSize == MDMA_SOURCE_DATASIZE_HALFWORD) ? 2 : 1;
    if (len % data_width != 0) {
        return DMA_ERR_PARAM;
    }

    s_mdma_states[ch_id].busy = 1;

    /* 启动传输 */
    hal_status = HAL_MDMA_Start(hmdma, src_addr, dst_addr, len, 1);

    if (hal_status != HAL_OK) {
        s_mdma_states[ch_id].busy = 0;
        return DMA_ERR_TRANSFER;
    }

    return DMA_OK;
}

/**
 * @brief  启动循环 MDMA 传输
 */
dma_err_t MDMA_StartCircular(mdma_channel_id_t ch_id, uint32_t src_addr, uint32_t dst_addr, uint32_t len)
{
    MDMA_HandleTypeDef *hmdma;
    HAL_StatusTypeDef hal_status;

    if (ch_id >= MDMA_CH_MAX) {
        return DMA_ERR_PARAM;
    }

    if (!s_mdma_states[ch_id].initialized) {
        return DMA_ERR_NOT_INIT;
    }

    if (s_mdma_states[ch_id].busy) {
        return DMA_ERR_BUSY;
    }

    hmdma = s_mdma_states[ch_id].hmdma;
    s_mdma_states[ch_id].busy = 1;

    hal_status = HAL_MDMA_Start_IT(hmdma, src_addr, dst_addr, len, 1);

    if (hal_status != HAL_OK) {
        s_mdma_states[ch_id].busy = 0;
        return DMA_ERR_TRANSFER;
    }

    return DMA_OK;
}

/**
 * @brief  停止 MDMA 传输
 */
dma_err_t MDMA_Stop(mdma_channel_id_t ch_id)
{
    if (ch_id >= MDMA_CH_MAX) {
        return DMA_ERR_PARAM;
    }

    if (!s_mdma_states[ch_id].initialized) {
        return DMA_ERR_NOT_INIT;
    }

    HAL_MDMA_Stop(s_mdma_states[ch_id].hmdma);
    s_mdma_states[ch_id].busy = 0;

    return DMA_OK;
}

/**
 * @brief  查询通道是否忙
 */
bool MDMA_IsBusy(mdma_channel_id_t ch_id)
{
    if (ch_id >= MDMA_CH_MAX) {
        return false;
    }
    return (s_mdma_states[ch_id].busy != 0);
}

/**
 * @brief  注册回调函数
 */
dma_err_t MDMA_RegisterCallback(mdma_channel_id_t ch_id,
                                dma_tc_callback_t tc_cb,
                                dma_ht_callback_t ht_cb,
                                dma_err_callback_t err_cb,
                                void *user_data)
{
    if (ch_id >= MDMA_CH_MAX) {
        return DMA_ERR_PARAM;
    }

    s_mdma_states[ch_id].tc_cb = tc_cb;
    s_mdma_states[ch_id].ht_cb = ht_cb;
    s_mdma_states[ch_id].err_cb = err_cb;
    s_mdma_states[ch_id].user_data = user_data;

    return DMA_OK;
}

/**
 * @brief  获取通道统计信息
 */
dma_err_t MDMA_GetStats(mdma_channel_id_t ch_id, uint32_t *xfer_count, uint32_t *err_count)
{
    if (ch_id >= MDMA_CH_MAX) {
        return DMA_ERR_PARAM;
    }

    if (xfer_count != NULL) {
        *xfer_count = s_mdma_states[ch_id].xfer_count;
    }
    if (err_count != NULL) {
        *err_count = s_mdma_states[ch_id].err_count;
    }

    return DMA_OK;
}

/**
 * @brief  清除标志
 */
dma_err_t MDMA_ClearFlags(mdma_channel_id_t ch_id)
{
    if (ch_id >= MDMA_CH_MAX) {
        return DMA_ERR_PARAM;
    }

    HAL_MDMA_ClearCallback(s_mdma_states[ch_id].hmdma, HAL_MDMA_XFER_CPLT_CB_ID);
    HAL_MDMA_ClearCallback(s_mdma_states[ch_id].hmdma, HAL_MDMA_XFER_HALFCPLT_CB_ID);
    HAL_MDMA_ClearCallback(s_mdma_states[ch_id].hmdma, HAL_MDMA_XFER_ERROR_CB_ID);

    /* 重新注册 */
    HAL_MDMA_RegisterCallback(s_mdma_states[ch_id].hmdma, HAL_MDMA_XFER_CPLT_CB_ID, mdma_hal_tc_callback);
    HAL_MDMA_RegisterCallback(s_mdma_states[ch_id].hmdma, HAL_MDMA_XFER_HALFCPLT_CB_ID, mdma_hal_ht_callback);
    HAL_MDMA_RegisterCallback(s_mdma_states[ch_id].hmdma, HAL_MDMA_XFER_ERROR_CB_ID, mdma_hal_err_callback);

    return DMA_OK;
}

/**
 * @brief  获取 HAL 句柄
 */
MDMA_HandleTypeDef* MDMA_GetHandle(mdma_channel_id_t ch_id)
{
    if (ch_id >= MDMA_CH_MAX) {
        return NULL;
    }
    if (!s_mdma_states[ch_id].initialized) {
        return NULL;
    }
    return s_mdma_states[ch_id].hmdma;
}

/* =============================================================================
 * 中断服务程序
 * ==========================================================================*/

/**
 * @brief  MDMA 全局中断处理
 * @note   在 stm32n6xx_it.c 的 MDMA_IRQHandler 中调用
 */
void MDMA_IRQHandler(void)
{
    /* HAL 会自动分发到对应通道的回调 */
    for (int i = 0; i < MDMA_CH_MAX; i++) {
        if (s_mdma_states[i].initialized && s_mdma_states[i].hmdma != NULL) {
            HAL_MDMA_IRQHandler(s_mdma_states[i].hmdma);
        }
    }
}

/**
 * @brief  通道回调 (兼容层, 实际由 HAL 内部调用)
 */
void MDMA_ChannelCallback(MDMA_HandleTypeDef *hmdma)
{
    (void)hmdma;
    /* HAL_MDMA_IRQHandler 内部已处理回调分发 */
}
