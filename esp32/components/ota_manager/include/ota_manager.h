#pragma once
/**
 * @file ota_manager.h
 * @brief OTA 升级管理组件
 * @version 3.2
 *
 * 支持：
 * - 通过 WiFi/BLE 接收固件
 * - 双分区 OTA（A/B 分区）
 * - 固件校验和回滚机制
 * - 与 STM32 协同 OTA
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lingxi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * 配置常量
 *===========================================================================*/

#define OTA_CHUNK_SIZE          1024    /**< 固件数据块大小 */
#define OTA_MAX_FIRMWARE_SIZE   (4 * 1024 * 1024)  /**< 最大固件 4MB */
#define OTA_VERIFY_BUF_SIZE     4096    /**< 校验缓冲区 */

/*=============================================================================
 * 类型定义
 *===========================================================================*/

/**
 * @brief OTA 状态
 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_PREPARE,          /**< 准备接收 */
    OTA_STATE_RECEIVING,          /**< 接收中 */
    OTA_STATE_VERIFYING,          /**< 校验中 */
    OTA_STATE_VERIFY_OK,          /**< 校验通过 */
    OTA_STATE_VERIFY_FAIL,        /**< 校验失败 */
    OTA_STATE_COMMITTING,         /**< 提交中 */
    OTA_STATE_COMPLETE,           /**< 完成 */
    OTA_STATE_ROLLBACK,           /**< 回滚 */
    OTA_STATE_ERROR,              /**< 错误 */
} ota_state_t;

/**
 * @brief OTA 来源
 */
typedef enum {
    OTA_SOURCE_WIFI = 0,          /**< WiFi OTA */
    OTA_SOURCE_BLE,               /**< BLE OTA */
    OTA_SOURCE_SDIO,              /**< SDIO OTA (来自 STM32) */
} ota_source_t;

/**
 * @brief OTA 配置
 */
typedef struct {
    uint32_t firmware_size;       /**< 固件大小 */
    uint32_t crc32;               /**< CRC32 校验值 */
    uint8_t  version[16];         /**< 版本号 */
    ota_source_t source;          /**< OTA 来源 */
} ota_config_t;

/**
 * @brief OTA 进度回调
 */
typedef void (*ota_progress_cb_t)(uint8_t progress, ota_state_t state, void *arg);

/**
 * @brief OTA 完成回调
 */
typedef void (*ota_complete_cb_t)(bool success, const char *version, void *arg);

/*=============================================================================
 * API 函数
 *===========================================================================*/

/**
 * @brief 初始化 OTA 管理器
 */
esp_err_t ota_manager_init(void);

/**
 * @brief 反初始化
 */
esp_err_t ota_manager_deinit(void);

/**
 * @brief 注册回调
 */
esp_err_t ota_manager_register_callbacks(ota_progress_cb_t progress_cb,
                                        ota_complete_cb_t complete_cb, void *arg);

/**
 * @brief 启动 OTA 准备
 * @param config OTA 配置
 */
esp_err_t ota_manager_prepare(const ota_config_t *config);

/**
 * @brief 接收固件数据块
 * @param chunk 数据块
 * @return ESP_OK 成功，ESP_ERR_INVALID_SIZE 大小错误
 */
esp_err_t ota_manager_receive_chunk(const lx_payload_ota_chunk_t *chunk);

/**
 * @brief 校验固件
 * @return ESP_OK 校验通过
 */
esp_err_t ota_manager_verify(void);

/**
 * @brief 提交固件（切换分区）
 * @return ESP_OK 成功
 */
esp_err_t ota_manager_commit(void);

/**
 * @brief 回滚到上一版本
 */
esp_err_t ota_manager_rollback(void);

/**
 * @brief 取消 OTA
 */
esp_err_t ota_manager_cancel(void);

/**
 * @brief 获取当前状态
 */
ota_state_t ota_manager_get_state(void);

/**
 * @brief 获取当前进度 (0-100)
 */
uint8_t ota_manager_get_progress(void);

/**
 * @brief 获取当前运行分区
 */
esp_err_t ota_manager_get_running_partition(char *label, size_t len);

/**
 * @brief 获取当前 OTA 信息
 */
esp_err_t ota_manager_get_info(ota_config_t *info);

/**
 * @brief 通过 WiFi 启动 OTA 下载
 * @param url 固件下载地址
 */
esp_err_t ota_manager_start_wifi_download(const char *url);

/**
 * @brief 处理 BLE OTA 数据
 * @param data 数据
 * @param len 长度
 */
esp_err_t ota_manager_process_ble_data(const uint8_t *data, uint16_t len);

/**
 * @brief 通知 STM32 OTA 状态
 * 通过 SDIO 发送 OTA 状态帧
 */
esp_err_t ota_manager_notify_stm32(void);

#ifdef __cplusplus
}
#endif
