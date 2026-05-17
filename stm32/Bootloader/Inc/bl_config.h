/*******************************************************************************
 * @file    bl_config.h
 * @brief   Lingxi N6 AI Deck — Bootloader 配置与分区定义
 * @version 1.1.0
 * @date    2026-05-16
 *
 * 修正说明 (v1.1.0):
 *   STM32N657 没有用户可写的 Internal Flash。0x08000000 是 128KB Boot ROM(只读)。
 *   所有代码(Bootloader + App)必须放在外部 XIP Flash(W25Q512)中。
 *   Bootloader 由 Boot ROM 从 XIP Flash 加载到 RAM 执行，因此可以擦写 XIP Flash。
 ******************************************************************************/

#ifndef BL_CONFIG_H
#define BL_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 系统配置
 *===========================================================================*/
#define BL_VERSION_MAJOR        1
#define BL_VERSION_MINOR        1
#define BL_VERSION_PATCH        0

/* 超时配置 */
#define BL_BOOT_TIMEOUT_MS      3000    /* 上电后监听 UART 升级命令超时 */
#define BL_CHUNK_TIMEOUT_MS     5000    /* 数据块接收超时 */
#define BL_WDG_TIMEOUT_MS       10000   /* 看门狗超时 */

/* UART 配置 */
#define BL_UART_BAUDRATE        115200U
#define BL_UART_INSTANCE        USART1  /* PA9/PA10, 与飞控通信 */
#define BL_UART_TX_BUF_SIZE     256
#define BL_UART_RX_BUF_SIZE     1536    /* > 最大帧长 */

/*============================================================================
 * STM32N6 启动模式 (RM0486)
 *===========================================================================
 * BOOT0=0, BOOT1=0 -> Flash boot  (从 XIP Flash 启动，正常运行模式)
 * BOOT0=1, BOOT1=0 -> Serial boot (内置 System Bootloader，串口下载)
 * BOOT0=X, BOOT1=1 -> Development boot (安全调试循环)
 *
 * 本 Bootloader 工作在 BOOT0=0, BOOT1=0 模式。
 * Boot ROM 先从 XIP Flash 加载 Bootloader 到 RAM 执行，
 * Bootloader 再通过 UART OTA 升级 App。
 */

/*============================================================================
 * XIP Flash 分区定义 (W25Q512, 64MB OctoSPI)
 *===========================================================================
 *
 * 镜像头(Image Header)大小 = 0x400 (1KB)，由 STM32_SigningTool 生成。
 * 实际代码从 镜像头基址 + 0x400 开始。
 *
 * XIP Flash 地址映射:
 *   0x7000_0000 - 0x7000_03FF  Bootloader Image Header (1KB)
 *   0x7000_0400 - 0x7001_FFFF  Bootloader Code           (~124KB)
 *   0x7002_0000 - 0x7002_3FFF  Info Sector               (16KB)
 *   0x7002_4000 - 0x7017_FFFF  Reserved
 *   0x7018_0000 - 0x7018_03FF  App A Image Header        (1KB)
 *   0x7018_0400 - 0x7027_FFFF  App A Code                (~1MB)
 *   0x7028_0000 - 0x7028_03FF  App B Image Header        (1KB)
 *   0x7028_0400 - 0x7037_FFFF  App B Code                (~1MB)
 */

#define BL_IMAGE_HEADER_SIZE    0x400UL     /* 1KB STM32 镜像头 */

#define BL_XIP_FLASH_BASE       0x70000000UL
#define BL_XIP_FLASH_SIZE       (64 * 1024 * 1024UL)  /* 64MB */

/* --- Bootloader 区域 (由 Boot ROM 加载) --- */
#define BL_BOOTLOADER_BASE      0x70000000UL
#define BL_BOOTLOADER_ADDR      (BL_BOOTLOADER_BASE + BL_IMAGE_HEADER_SIZE)
#define BL_BOOTLOADER_SIZE      (128 * 1024UL)          /* 128KB 总空间(含头) */
#define BL_BOOTLOADER_CODE_SIZE (BL_BOOTLOADER_SIZE - BL_IMAGE_HEADER_SIZE)

/* --- Info Sector (非易失配置区，放在 XIP Flash) --- */
#define BL_INFO_SECTOR_ADDR     0x70020000UL
#define BL_INFO_SECTOR_SIZE     (16 * 1024UL)            /* 16KB = 4 x 4KB W25Q sectors */

/* --- OTA 接收缓存 (使用 RAM，N6 没有用户 Internal Flash) --- */
#define BL_OTA_RAM_CACHE_ADDR   0x24000000UL             /* AXISRAM 起始 */
#define BL_OTA_RAM_CACHE_SIZE   (1 * 1024 * 1024UL)      /* 1MB RAM 缓存 */

/* --- App A/B 分区 --- */
#define BL_APP_A_BASE           0x70180000UL
#define BL_APP_A_ADDR           (BL_APP_A_BASE + BL_IMAGE_HEADER_SIZE)
#define BL_APP_A_SIZE           (1 * 1024 * 1024UL)     /* 1MB 总空间(含头) */
#define BL_APP_A_CODE_SIZE      (BL_APP_A_SIZE - BL_IMAGE_HEADER_SIZE)

#define BL_APP_B_BASE           0x70280000UL
#define BL_APP_B_ADDR           (BL_APP_B_BASE + BL_IMAGE_HEADER_SIZE)
#define BL_APP_B_SIZE           (1 * 1024 * 1024UL)     /* 1MB 总空间(含头) */
#define BL_APP_B_CODE_SIZE      (BL_APP_B_SIZE - BL_IMAGE_HEADER_SIZE)

/* --- W25Q512 参数 --- */
#define W25Q_PAGE_SIZE          256
#define W25Q_SECTOR_SIZE        4096
#define W25Q_BLOCK_SIZE         65536

/*============================================================================
 * Info Sector 结构
 *===========================================================================*/
#define BL_INFO_MAGIC           0x4C424E58UL   /* "XLNB" = XingLing N Boot */
#define BL_INFO_VERSION         0x0002         /* v1.1.0 升级版本 */

typedef struct __attribute__((packed)) {
    uint32_t magic;                 /* BL_INFO_MAGIC */
    uint16_t struct_version;        /* BL_INFO_VERSION */
    uint8_t  active_partition;      /* 0=AppA, 1=AppB */
    uint8_t  reserved0;

    struct {
        uint32_t size;              /* 固件大小(不含镜像头) */
        uint32_t crc32;
        uint8_t  version[16];
        uint32_t timestamp;
    } app_a;

    struct {
        uint32_t size;
        uint32_t crc32;
        uint8_t  version[16];
        uint32_t timestamp;
    } app_b;

    uint32_t boot_count;
    uint32_t watchdog_count;
    uint8_t  rollback_request;
    uint8_t  upgrade_pending;
    uint8_t  reserved1[2];

    uint32_t crc32;                 /* 整体 CRC32 (从 magic 到 reserved1) */
} bl_info_sector_t;

/*============================================================================
 * 状态码
 *===========================================================================*/
typedef enum {
    BL_OK = 0,
    BL_ERR_FLASH,
    BL_ERR_CRC,
    BL_ERR_TIMEOUT,
    BL_ERR_INVALID,
    BL_ERR_NO_SPACE,
    BL_ERR_VERIFY,
    BL_ERR_UART,
    BL_ERR_XSPI,
    BL_ERR_RAM,
} bl_err_t;

#ifdef __cplusplus
}
#endif

#endif /* BL_CONFIG_H */
