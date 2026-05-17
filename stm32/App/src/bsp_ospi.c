/**
  ******************************************************************************
  * @file    bsp_ospi.c
  * @brief   OctoSPI Flash 驱动实现 (W25Q512, 8线)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 使用 HAL OSPI 驱动
  * - 支持 OctoSPI 8线模式
  * - 自动分页写入, 自动擦除
  ******************************************************************************
  */

#include "bsp_ospi.h"

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static OSPI_HandleTypeDef hospi;
static volatile uint8_t s_ospi_busy = 0;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void ospi_gpio_init(void);
static void ospi_clock_init(void);
static lingxi_err_t ospi_flash_reset(void);
static lingxi_err_t ospi_wait_ready(uint32_t timeout_ms);
static lingxi_err_t ospi_write_enable(void);
static lingxi_err_t ospi_auto_erase(uint32_t addr, uint32_t len);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 OctoSPI Flash
 */
lingxi_err_t bsp_ospi_init(void)
{
    /* 时钟初始化 */
    ospi_clock_init();

    /* GPIO 初始化 */
    ospi_gpio_init();

    /* OSPI 初始化 */
    hospi.Instance = OCTOSPI1;

    hospi.Init.FifoThreshold         = 4;
    hospi.Init.DualQuad              = HAL_OSPI_DUALQUAD_DISABLE;
    hospi.Init.MemoryType            = HAL_OSPI_MEMTYPE_MICRON;
    hospi.Init.DeviceSize            = 24;  /* 2^24 = 64MB */
    hospi.Init.ChipSelectHighTime    = 2;
    hospi.Init.FreeRunningClock      = HAL_OSPI_FREERUNCLK_DISABLE;
    hospi.Init.ClockMode             = HAL_OSPI_CLOCK_MODE_0;
    hospi.Init.WrapSize              = HAL_OSPI_WRAP_NOT_SUPPORTED;
    hospi.Init.ClockPrescaler        = 2;    /* HCLK/3, 约 266MHz/3 = 88MHz */
    hospi.Init.SampleShifting        = HAL_OSPI_SAMPLE_SHIFTING_NONE;
    hospi.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_DISABLE;
    hospi.Init.ChipSelectBoundary    = 0;

    if (HAL_OSPI_Init(&hospi) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* 复位 Flash */
    lingxi_err_t err = ospi_flash_reset();
    LX_RETURN_IF_ERR(err);

    /* 验证 Flash ID */
    uint32_t flash_id = 0;
    err = bsp_ospi_read_id(&flash_id);
    LX_RETURN_IF_ERR(err);

    if ((flash_id & 0xFF0000) != 0xEF0000) { /* Winbond manufacturer ID */
        LX_ERR_PRINT("Flash ID mismatch: expected Winbond, got 0x%06X", flash_id);
        return LINGXI_ERR_IO;
    }

    LX_DEBUG_PRINT("OctoSPI Flash initialized: W25Q512, ID=0x%06X", flash_id);
    return LINGXI_OK;
}

/**
 * @brief  读取 Flash ID
 */
lingxi_err_t bsp_ospi_read_id(uint32_t *id)
{
    LX_RETURN_IF_NULL(id);

    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_RDID;
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_OSPI_DATA_1_LINE;
    cmd.NbData             = 3;
    cmd.DummyCycles        = 0;

    uint8_t id_buf[3] = {0};

    if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    if (HAL_OSPI_Receive(&hospi, id_buf, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    *id = ((uint32_t)id_buf[0] << 16) | ((uint32_t)id_buf[1] << 8) | id_buf[2];
    return LINGXI_OK;
}

/**
 * @brief  读取数据
 */
lingxi_err_t bsp_ospi_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    LX_RETURN_IF_NULL(buf);

    if (addr + len > OSPI_FLASH_SIZE) {
        return LINGXI_ERR_INVALID_PARAM;
    }

    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_8READ;  /* Octal fast read */
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES;
    cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    cmd.Address            = addr;
    cmd.AddressMode        = HAL_OSPI_ADDRESS_8_LINES;
    cmd.AddressSize        = HAL_OSPI_ADDRESS_32_BITS;
    cmd.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
    cmd.DataMode           = HAL_OSPI_DATA_8_LINES;
    cmd.NbData             = len;
    cmd.DummyCycles        = 6;  /* 根据 datasheet */
    cmd.DQSMode            = HAL_OSPI_DQS_DISABLE;

    if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    if (HAL_OSPI_Receive(&hospi, buf, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return LINGXI_OK;
}

/**
 * @brief  写入数据 (自动分页)
 */
lingxi_err_t bsp_ospi_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    LX_RETURN_IF_NULL(buf);

    if (addr + len > OSPI_FLASH_SIZE) {
        return LINGXI_ERR_INVALID_PARAM;
    }

    /* 自动擦除覆盖区域 */
    lingxi_err_t err = ospi_auto_erase(addr, len);
    LX_RETURN_IF_ERR(err);

    uint32_t offset = 0;
    while (offset < len) {
        uint32_t page_addr = addr + offset;
        uint32_t page_offset = page_addr % OSPI_PAGE_SIZE;
        uint32_t page_remain = OSPI_PAGE_SIZE - page_offset;
        uint32_t chunk = (len - offset < page_remain) ? (len - offset) : page_remain;

        /* Write Enable */
        err = ospi_write_enable();
        LX_RETURN_IF_ERR(err);

        /* Page Program */
        OSPI_RegularCmdTypeDef cmd = {0};
        cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
        cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
        cmd.Instruction        = W25Q_CMD_8PP;
        cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES;
        cmd.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
        cmd.Address            = page_addr;
        cmd.AddressMode        = HAL_OSPI_ADDRESS_8_LINES;
        cmd.AddressSize        = HAL_OSPI_ADDRESS_32_BITS;
        cmd.DataMode           = HAL_OSPI_DATA_8_LINES;
        cmd.NbData             = chunk;
        cmd.DummyCycles        = 0;

        if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
            return LINGXI_ERR_IO;
        }

        if (HAL_OSPI_Transmit(&hospi, (uint8_t*)buf + offset, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
            return LINGXI_ERR_IO;
        }

        /* 等待写入完成 */
        err = ospi_wait_ready(100);
        LX_RETURN_IF_ERR(err);

        offset += chunk;
    }

    return LINGXI_OK;
}

/**
 * @brief  擦除扇区 (4KB)
 */
lingxi_err_t bsp_ospi_erase_sector(uint32_t addr)
{
    if (addr % OSPI_SECTOR_SIZE != 0) {
        return LINGXI_ERR_INVALID_PARAM;
    }

    lingxi_err_t err = ospi_write_enable();
    LX_RETURN_IF_ERR(err);

    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_SE;
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.Address            = addr;
    cmd.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
    cmd.AddressSize        = HAL_OSPI_ADDRESS_32_BITS;
    cmd.DataMode           = HAL_OSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return ospi_wait_ready(400);  /* 扇区擦除约 400ms */
}

/**
 * @brief  擦除块 (64KB)
 */
lingxi_err_t bsp_ospi_erase_block(uint32_t addr)
{
    if (addr % OSPI_BLOCK_SIZE != 0) {
        return LINGXI_ERR_INVALID_PARAM;
    }

    lingxi_err_t err = ospi_write_enable();
    LX_RETURN_IF_ERR(err);

    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_BE;
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.Address            = addr;
    cmd.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
    cmd.AddressSize        = HAL_OSPI_ADDRESS_32_BITS;
    cmd.DataMode           = HAL_OSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return ospi_wait_ready(2000);  /* 块擦除约 2s */
}

/**
 * @brief  整片擦除
 */
lingxi_err_t bsp_ospi_erase_chip(void)
{
    lingxi_err_t err = ospi_write_enable();
    LX_RETURN_IF_ERR(err);

    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_CE;
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_OSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return ospi_wait_ready(20000);  /* 整片擦除约 20s */
}

/**
 * @brief  启用 XIP 模式
 */
lingxi_err_t bsp_ospi_enable_xip(void)
{
    /* 配置 OSPI 为内存映射模式 */
    OSPI_MemoryMappedTypeDef mmap_cfg = {0};
    mmap_cfg.TimeOutActivation = HAL_OSPI_TIMEOUT_COUNTER_DISABLE;
    mmap_cfg.TimeOutPeriod     = 0;

    if (HAL_OSPI_MemoryMapped(&hospi, &mmap_cfg) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return LINGXI_OK;
}

/**
 * @brief  禁用 XIP 模式
 */
lingxi_err_t bsp_ospi_disable_xip(void)
{
    /* 退出内存映射模式 */
    if (HAL_OSPI_Abort(&hospi) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return LINGXI_OK;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  GPIO 初始化
 */
static void ospi_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OCTOSPI1;

    /* CLK: PB1 */
    GPIO_InitStruct.Pin = OSPIM_CLK_PIN;
    HAL_GPIO_Init(OSPIM_GPIO_PORT_CLK, &GPIO_InitStruct);

    /* D0-D1: PD6-PD7 */
    GPIO_InitStruct.Pin = OSPIM_D0_PIN | OSPIM_D1_PIN;
    HAL_GPIO_Init(OSPIM_GPIO_PORT_D0_D1, &GPIO_InitStruct);

    /* D2-D7: PE9-PE14 */
    GPIO_InitStruct.Pin = OSPIM_D2_PIN | OSPIM_D3_PIN | OSPIM_D4_PIN |
                          OSPIM_D5_PIN | OSPIM_D6_PIN | OSPIM_D7_PIN;
    HAL_GPIO_Init(OSPIM_GPIO_PORT_D2_D7, &GPIO_InitStruct);

    /* NCS: PB6 */
    GPIO_InitStruct.Pin = OSPIM_NCS_PIN;
    GPIO_InitStruct.Alternate = GPIO_AF10_OCTOSPI1;
    HAL_GPIO_Init(OSPIM_GPIO_PORT_NCS, &GPIO_InitStruct);
}

/**
 * @brief  时钟初始化
 */
static void ospi_clock_init(void)
{
    __HAL_RCC_OCTOSPI1_CLK_ENABLE();
    __HAL_RCC_OCTOSPI1_FORCE_RESET();
    __HAL_RCC_OCTOSPI1_RELEASE_RESET();
}

/**
 * @brief  Flash 复位
 */
static lingxi_err_t ospi_flash_reset(void)
{
    OSPI_RegularCmdTypeDef cmd = {0};

    /* Enable Reset */
    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = 0x66;
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_OSPI_DATA_NONE;

    if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    /* Reset Device */
    cmd.Instruction = 0x99;
    if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    HAL_Delay(1);  /* 等待复位完成 */

    return LINGXI_OK;
}

/**
 * @brief  等待 Flash 就绪
 */
static lingxi_err_t ospi_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t sr = 0;

    do {
        OSPI_RegularCmdTypeDef cmd = {0};
        cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
        cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
        cmd.Instruction        = W25Q_CMD_RDSR1;
        cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
        cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
        cmd.DataMode           = HAL_OSPI_DATA_1_LINE;
        cmd.NbData             = 1;
        cmd.DummyCycles        = 0;

        if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
            return LINGXI_ERR_IO;
        }

        if (HAL_OSPI_Receive(&hospi, &sr, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
            return LINGXI_ERR_IO;
        }

        if ((sr & 0x01) == 0) {
            return LINGXI_OK;  /* WIP bit = 0, ready */
        }

    } while ((HAL_GetTick() - start) < timeout_ms);

    return LINGXI_ERR_TIMEOUT;
}

/**
 * @brief  写使能
 */
static lingxi_err_t ospi_write_enable(void)
{
    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_WREN;
    cmd.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.AddressMode        = HAL_OSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_OSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_OSPI_Command(&hospi, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return LINGXI_ERR_IO;
    }

    return LINGXI_OK;
}

/**
 * @brief  自动擦除 (根据地址和长度)
 */
static lingxi_err_t ospi_auto_erase(uint32_t addr, uint32_t len)
{
    uint32_t start_sector = addr / OSPI_SECTOR_SIZE;
    uint32_t end_sector = (addr + len - 1) / OSPI_SECTOR_SIZE;

    for (uint32_t sector = start_sector; sector <= end_sector; sector++) {
        lingxi_err_t err = bsp_ospi_erase_sector(sector * OSPI_SECTOR_SIZE);
        LX_RETURN_IF_ERR(err);
    }

    return LINGXI_OK;
}
