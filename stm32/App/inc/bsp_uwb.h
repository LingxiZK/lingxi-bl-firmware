/**
  ******************************************************************************
  * @file    bsp_uwb.h
  * @brief   DWM3000 UWB 驱动头文件 (SPI1, PA4-PA7 + PA8/PA11/PA12)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - DWM3000: Decawave/Qorvo DW3000 系列 UWB 收发器
  * - SPI1 @ 10MHz
  * - 支持 TWR (Two-Way Ranging) 测距
  ******************************************************************************
  */

#ifndef __BSP_UWB_H
#define __BSP_UWB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lingxi_bl.h"

/* =============================================================================
 * UWB 配置参数
 * ==========================================================================*/
#define UWB_SPI_BAUDRATE            10000000        /* 10 MHz */
#define UWB_CHANNEL                 5               /* UWB Channel 5 (6.5GHz) */
#define UWB_PREAMBLE_LEN            DWT_PLEN_64     /* 64 symbol preamble */
#define UWB_DATA_RATE               DWT_BR_6M8      /* 6.8 Mbps */
#define UWB_PAC_SIZE                DWT_PAC8         /* 8 symbol PAC */
#define UWB_TX_PREAMBLE_CODE        9               /* Channel 5 preamble code */
#define UWB_RX_PREAMBLE_CODE        9

/* 测距配置 */
#define UWB_RANGING_MODE            TWR_MODE         /* Two-Way Ranging */
#define UWB_MAX_ANCHORS            4                /* 最大锚点数量 */
#define UWB_RANGING_PERIOD_MS      100              /* 测距周期 100ms = 10Hz */

/* =============================================================================
 * 数据结构
 * ==========================================================================*/

typedef struct {
    uint16_t anchor_id;
    float distance_m;           /* 距离 (米) */
    float rx_power_dbm;         /* 接收信号强度 */
    float fp_power_dbm;         /* 首径功率 */
    uint32_t timestamp_ms;      /* 时间戳 */
    uint8_t valid;              /* 数据有效标志 */
} uwb_ranging_result_t;

typedef struct {
    uwb_ranging_result_t anchors[UWB_MAX_ANCHORS];
    uint8_t num_anchors;
    uint32_t sequence_num;
} uwb_ranging_data_t;

/* 回调函数类型 */
typedef void (*uwb_ranging_callback_t)(const uwb_ranging_data_t *data);

/* =============================================================================
 * API 函数声明
 * ==========================================================================*/

/**
 * @brief  初始化 DWM3000 UWB 模块
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_uwb_init(void);

/**
 * @brief  启动 UWB 测距
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_uwb_start_ranging(void);

/**
 * @brief  停止 UWB 测距
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_uwb_stop_ranging(void);

/**
 * @brief  注册测距结果回调
 * @param  callback: 回调函数
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_uwb_register_callback(uwb_ranging_callback_t callback);

/**
 * @brief  获取最新测距结果
 * @param  data: 输出数据结构
 * @retval LINGXI_OK 成功
 */
lingxi_err_t bsp_uwb_get_ranging_data(uwb_ranging_data_t *data);

/**
 * @brief  UWB 中断服务程序
 */
void bsp_uwb_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_UWB_H */
