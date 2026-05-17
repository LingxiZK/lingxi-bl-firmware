/**
  ******************************************************************************
  * @file    bsp_sdmmc2.c
  * @brief   SDMMC2 microSD 驱动实现
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - SDMMC2 原生控制器，4-bit 模式
  * - 支持 SD/SDHC/SDXC
  ******************************************************************************
  */

#include "bsp_sdmmc2.h"
#include "stm32n6xx_hal_sd.h"

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static SD_HandleTypeDef hsd2;
static volatile uint8_t s_sdmmc2_initialized = 0;
static volatile uint8_t s_sdmmc2_card_present = 0;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static lingxi_err_t sdmmc2_card_init(void);
static void sdmmc2_irq_handler(void);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 SDMMC2
 */
lingxi_err_t bsp_sdmmc2_init(void)
{
    HAL_StatusTypeDef status;

    /* 检查卡是否插入 */
    if (!bsp_sdmmc2_is_present()) {
        LX_ERR_PRINT("SDMMC2: no card detected");
        return LINGXI_ERR_IO;
    }

    /* 启动时钟 */
    __HAL_RCC_SDMMC2_CLK_ENABLE();
    __HAL_RCC_SDMMC2_FORCE_RESET();
    __HAL_RCC_SDMMC2_RELEASE_RESET();

    /* 配置 SDMMC2 */
    hsd2.Instance = SDMMC2_PERIPH;
    hsd2.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd2.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd2.Init.BusWide = SDMMC_BUS_WIDE_4B;
    hsd2.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd2.Init.ClockDiv = 2;  /* 200MHz / (2+2) = 50MHz */

    status = HAL_SD_Init(&hsd2);
    if (status != HAL_OK) {
        LX_ERR_PRINT("SDMMC2 HAL_SD_Init failed: %d", status);
        return LINGXI_ERR_IO;
    }

    /* 切换到 4-bit 模式 */
    status = HAL_SD_ConfigWideBusOperation(&hsd2, SDMMC_BUS_WIDE_4B);
    if (status != HAL_OK) {
        LX_ERR_PRINT("SDMMC2 wide bus config failed: %d", status);
        HAL_SD_DeInit(&hsd2);
        return LINGXI_ERR_IO;
    }

    s_sdmmc2_initialized = 1;
    s_sdmmc2_card_present = 1;

    LX_DEBUG_PRINT("SDMMC2 initialized, clock=%lu Hz", HAL_SD_GetCardState(&hsd2));
    return LINGXI_OK;
}

/**
 * @brief  检测卡是否插入
 */
bool bsp_sdmmc2_is_present(void)
{
    return (HAL_GPIO_ReadPin(SDMMC2_CD_GPIO_PORT, SDMMC2_CD_PIN) == GPIO_PIN_RESET);
}

/**
 * @brief  读取块
 */
lingxi_err_t bsp_sdmmc2_read_blocks(uint32_t block_addr, uint8_t *buf, uint32_t count)
{
    LX_RETURN_IF_NULL(buf);

    if (!s_sdmmc2_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    if (!bsp_sdmmc2_is_present()) {
        return LINGXI_ERR_IO;
    }

    HAL_StatusTypeDef status = HAL_SD_ReadBlocks(&hsd2, buf, block_addr, count, SDMMC2_TIMEOUT_MS);
    if (status != HAL_OK) {
        LX_ERR_PRINT("SDMMC2 read failed: %d", status);
        return LINGXI_ERR_IO;
    }

    /* 等待传输完成 */
    while (HAL_SD_GetCardState(&hsd2) != HAL_SD_CARD_TRANSFER);

    return LINGXI_OK;
}

/**
 * @brief  写入块
 */
lingxi_err_t bsp_sdmmc2_write_blocks(uint32_t block_addr, const uint8_t *buf, uint32_t count)
{
    LX_RETURN_IF_NULL(buf);

    if (!s_sdmmc2_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    if (!bsp_sdmmc2_is_present()) {
        return LINGXI_ERR_IO;
    }

    HAL_StatusTypeDef status = HAL_SD_WriteBlocks(&hsd2, (uint8_t *)buf, block_addr, count, SDMMC2_TIMEOUT_MS);
    if (status != HAL_OK) {
        LX_ERR_PRINT("SDMMC2 write failed: %d", status);
        return LINGXI_ERR_IO;
    }

    /* 等待传输完成 */
    while (HAL_SD_GetCardState(&hsd2) != HAL_SD_CARD_TRANSFER);

    return LINGXI_OK;
}

/**
 * @brief  获取卡信息
 */
lingxi_err_t bsp_sdmmc2_get_info(sdmmc2_info_t *info)
{
    LX_RETURN_IF_NULL(info);

    if (!s_sdmmc2_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    HAL_SD_CardInfoTypeDef card_info;
    HAL_StatusTypeDef status = HAL_SD_GetCardInfo(&hsd2, &card_info);
    if (status != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    info->capacity_mb = (uint32_t)(card_info.LogBlockNbr * card_info.LogBlockSize / (1024 * 1024));
    info->card_type = (uint8_t)card_info.CardType;
    info->clock_speed = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC2);

    return LINGXI_OK;
}

/**
 * @brief  检查是否忙碌
 */
bool bsp_sdmmc2_is_busy(void)
{
    if (!s_sdmmc2_initialized) {
        return false;
    }
    return (HAL_SD_GetCardState(&hsd2) != HAL_SD_CARD_TRANSFER);
}
