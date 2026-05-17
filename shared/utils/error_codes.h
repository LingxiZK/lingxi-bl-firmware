#pragma once
/**
 * @file error_codes.h
 * @brief 错误码定义
 * @version 3.2
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 通用错误码 */
#define LX_OK                   0
#define LX_ERR_GENERIC          -1
#define LX_ERR_NO_MEM           -2
#define LX_ERR_INVALID_ARG      -3
#define LX_ERR_INVALID_STATE    -4
#define LX_ERR_INVALID_SIZE     -5
#define LX_ERR_TIMEOUT          -6
#define LX_ERR_NOT_FOUND        -7
#define LX_ERR_NOT_SUPPORTED    -8
#define LX_ERR_BUSY             -9
#define LX_ERR_CRC              -10
#define LX_ERR_IO               -11
#define LX_ERR_COMM             -12

/* SDIO 错误码 */
#define LX_ERR_SDIO_TIMEOUT     -100
#define LX_ERR_SDIO_CRC         -101
#define LX_ERR_SDIO_FIFO        -102

/* WiFi 错误码 */
#define LX_ERR_WIFI_CONNECT     -200
#define LX_ERR_WIFI_DISCONNECT  -201
#define LX_ERR_WIFI_SCAN        -202

/* BLE 错误码 */
#define LX_ERR_BLE_CONNECT      -300
#define LX_ERR_BLE_DISCONNECT   -301
#define LX_ERR_BLE_GATT         -302

/* OTA 错误码 */
#define LX_ERR_OTA_PREPARE      -400
#define LX_ERR_OTA_WRITE        -401
#define LX_ERR_OTA_VERIFY       -402
#define LX_ERR_OTA_COMMIT       -403
#define LX_ERR_OTA_ROLLBACK     -404

/**
 * @brief 获取错误码描述
 */
const char *lx_err_to_string(int err_code);

#ifdef __cplusplus
}
#endif
