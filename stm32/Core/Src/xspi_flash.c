/**
  ******************************************************************************
  * @file    xspi_flash.c
  * @brief   XSPI1 OctoSPI Flash HAL Driver Implementation (W25Q512, STM32N657)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - Target: STM32N657L0H3Q, XSPI1 (OctoSPI)
  * - Flash: W25Q512JV, 128Mbit = 64MB
  * - Mode: OctoSPI DTR (Double Transfer Rate)
  * - Clock: 100-133MHz
  * - Pin Mapping:
  *     CLK:  PB2
  *     D0-D7: PB0/PB1/PC13/PC2/PC3/PC4/PD11/PD12
  *     NCS:  PB6
  *
  * @decision_record
  * - DR-001: 使用 OctoSPI DTR 模式 (8-8-8) 实现最大吞吐量
  * - DR-002: 自动擦除策略: 写入前检查并自动擦除受影响扇区
  * - DR-003: 状态寄存器轮询使用 1-line 模式 (兼容性最好)
  * - DR-004: XIP 模式使用内存映射, 无需软件干预读取
  ******************************************************************************
  */

#include "xspi_flash.h"

/* =============================================================================
 * 私有宏
 * ==========================================================================*/
#define XSPI_FLASH_ASSERT(cond)     do { if (!(cond)) return XSPI_FLASH_ERR_INVALID_PARAM; } while(0)
#define XSPI_FLASH_CHECK_HANDLE(h)  do { if ((h) == NULL || (h)->hxspi == NULL) return XSPI_FLASH_ERR_NULL_PTR; } while(0)
#define XSPI_FLASH_CHECK_INIT(h)    do { if ((h)->state != XSPI_FLASH_STATE_INIT && \
                                             (h)->state != XSPI_FLASH_STATE_READY && \
                                             (h)->state != XSPI_FLASH_STATE_XIP) \
                                            return XSPI_FLASH_ERR_NOT_INIT; } while(0)

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static const char *s_err_strings[] = {
    "OK",
    "NULL pointer",
    "Invalid parameter",
    "Timeout",
    "Hardware init failed",
    "ID mismatch",
    "Not initialized",
    "Busy",
    "Write protected",
    "Erase failed",
    "Program failed",
    "XIP error",
    "Communication error",
};

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static xspi_flash_err_t xspi_flash_gpio_init(void);
static xspi_flash_err_t xspi_flash_clock_init(void);
static xspi_flash_err_t xspi_flash_hal_init(xspi_flash_handle_t *hxspi_flash);
static xspi_flash_err_t xspi_flash_wait_wip(xspi_flash_handle_t *hxspi_flash, uint32_t timeout_ms);
static xspi_flash_err_t xspi_flash_write_enable(xspi_flash_handle_t *hxspi_flash);
static xspi_flash_err_t xspi_flash_write_disable(xspi_flash_handle_t *hxspi_flash);
static xspi_flash_err_t xspi_flash_read_sr1(xspi_flash_handle_t *hxspi_flash, uint8_t *sr);
static xspi_flash_err_t xspi_flash_auto_erase(xspi_flash_handle_t *hxspi_flash, uint32_t addr, uint32_t len);
static xspi_flash_err_t xspi_flash_send_cmd(xspi_flash_handle_t *hxspi_flash, uint8_t cmd,
                                               uint32_t addr, uint8_t addr_mode,
                                               uint8_t data_mode, uint32_t dummy,
                                               uint8_t *buf, uint32_t len);

/* =============================================================================
 * API 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 XSPI1 Flash 驱动
 */
xspi_flash_err_t xspi_flash_init(xspi_flash_handle_t *hxspi_flash, XSPI_HandleTypeDef *hxspi)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi);

    if (hxspi_flash == NULL) {
        return XSPI_FLASH_ERR_NULL_PTR;
    }

    /* 清零句柄 */
    memset(hxspi_flash, 0, sizeof(xspi_flash_handle_t));
    hxspi_flash->hxspi = hxspi;
    hxspi_flash->state = XSPI_FLASH_STATE_RESET;

    /* 时钟初始化 */
    xspi_flash_err_t err = xspi_flash_clock_init();
    if (err != XSPI_FLASH_OK) {
        hxspi_flash->state = XSPI_FLASH_STATE_ERROR;
        return err;
    }

    /* GPIO 初始化 */
    err = xspi_flash_gpio_init();
    if (err != XSPI_FLASH_OK) {
        hxspi_flash->state = XSPI_FLASH_STATE_ERROR;
        return err;
    }

    /* HAL 初始化 */
    err = xspi_flash_hal_init(hxspi_flash);
    if (err != XSPI_FLASH_OK) {
        hxspi_flash->state = XSPI_FLASH_STATE_ERROR;
        return err;
    }

    /* 复位 Flash */
    err = xspi_flash_reset_device(hxspi_flash);
    if (err != XSPI_FLASH_OK) {
        hxspi_flash->state = XSPI_FLASH_STATE_ERROR;
        return err;
    }

    /* 读取并验证 ID */
    err = xspi_flash_read_id(hxspi_flash, &hxspi_flash->flash_id);
    if (err != XSPI_FLASH_OK) {
        hxspi_flash->state = XSPI_FLASH_STATE_ERROR;
        return err;
    }

    if (hxspi_flash->flash_id.manufacturer_id != 0xEF) {
        hxspi_flash->state = XSPI_FLASH_STATE_ERROR;
        return XSPI_FLASH_ERR_ID_MISMATCH;
    }

    hxspi_flash->state = XSPI_FLASH_STATE_INIT;
    hxspi_flash->mode = XSPI_FLASH_MODE_1LINE; /* 默认 1-line 模式 */

    return XSPI_FLASH_OK;
}

/**
 * @brief  反初始化 XSPI1 Flash 驱动
 */
xspi_flash_err_t xspi_flash_deinit(xspi_flash_handle_t *hxspi_flash)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);

    /* 退出 XIP 模式 */
    if (hxspi_flash->state == XSPI_FLASH_STATE_XIP) {
        xspi_flash_disable_xip(hxspi_flash);
    }

    /* 反初始化 HAL */
    HAL_XSPI_DeInit(hxspi_flash->hxspi);

    /* 关闭时钟 */
    __HAL_RCC_XSPI1_CLK_DISABLE();

    hxspi_flash->state = XSPI_FLASH_STATE_RESET;
    return XSPI_FLASH_OK;
}

/**
 * @brief  读取 Flash JEDEC ID
 */
xspi_flash_err_t xspi_flash_read_id(xspi_flash_handle_t *hxspi_flash, xspi_flash_id_t *id)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_ASSERT(id != NULL);

    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_RDID;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_XSPI_DATA_1_LINE;
    cmd.NbData             = 3;
    cmd.DummyCycles        = 0;

    uint8_t id_buf[3] = {0};

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    if (HAL_XSPI_Receive(hxspi_flash->hxspi, id_buf, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    id->manufacturer_id = id_buf[0];
    id->memory_type     = id_buf[1];
    id->capacity        = id_buf[2];
    id->jedec_id        = ((uint32_t)id_buf[0] << 16) | ((uint32_t)id_buf[1] << 8) | id_buf[2];

    return XSPI_FLASH_OK;
}

/**
 * @brief  读取数据 (1-line 标准读取)
 */
xspi_flash_err_t xspi_flash_read(xspi_flash_handle_t *hxspi_flash, uint32_t addr, uint8_t *buf, uint32_t len)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_CHECK_INIT(hxspi_flash);
    XSPI_FLASH_ASSERT(buf != NULL);
    XSPI_FLASH_ASSERT(addr + len <= XSPI_FLASH_SIZE);

    if (len == 0) {
        return XSPI_FLASH_OK;
    }

    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_READ;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    cmd.Address            = addr;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_1_LINE;
    cmd.AddressSize        = HAL_XSPI_ADDRESS_32_BITS;
    cmd.AddressDtrMode     = HAL_XSPI_ADDRESS_DTR_DISABLE;
    cmd.DataMode           = HAL_XSPI_DATA_1_LINE;
    cmd.NbData             = len;
    cmd.DummyCycles        = 0;
    cmd.DQSMode            = HAL_XSPI_DQS_DISABLE;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    if (HAL_XSPI_Receive(hxspi_flash->hxspi, buf, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    hxspi_flash->stats.read_count++;
    return XSPI_FLASH_OK;
}

/**
 * @brief  快速读取数据 (OctoSPI 8-line DTR)
 */
xspi_flash_err_t xspi_flash_fast_read(xspi_flash_handle_t *hxspi_flash, uint32_t addr, uint8_t *buf, uint32_t len)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_CHECK_INIT(hxspi_flash);
    XSPI_FLASH_ASSERT(buf != NULL);
    XSPI_FLASH_ASSERT(addr + len <= XSPI_FLASH_SIZE);

    if (len == 0) {
        return XSPI_FLASH_OK;
    }

    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_8READ_DTR;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
    cmd.Address            = addr;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    cmd.AddressSize        = HAL_XSPI_ADDRESS_32_BITS;
    cmd.AddressDtrMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    cmd.DataMode           = HAL_XSPI_DATA_8_LINES;
    cmd.DataDtrMode        = HAL_XSPI_DATA_DTR_ENABLE;
    cmd.NbData             = len;
    cmd.DummyCycles        = XSPI1_DUMMY_CYCLES_DTR;
    cmd.DQSMode            = HAL_XSPI_DQS_ENABLE;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    if (HAL_XSPI_Receive(hxspi_flash->hxspi, buf, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    hxspi_flash->stats.read_count++;
    return XSPI_FLASH_OK;
}

/**
 * @brief  写入数据 (自动分页, 自动擦除)
 */
xspi_flash_err_t xspi_flash_write(xspi_flash_handle_t *hxspi_flash, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_CHECK_INIT(hxspi_flash);
    XSPI_FLASH_ASSERT(buf != NULL);
    XSPI_FLASH_ASSERT(addr + len <= XSPI_FLASH_SIZE);

    if (len == 0) {
        return XSPI_FLASH_OK;
    }

    /* 自动擦除受影响区域 */
    xspi_flash_err_t err = xspi_flash_auto_erase(hxspi_flash, addr, len);
    if (err != XSPI_FLASH_OK) {
        return err;
    }

    uint32_t offset = 0;
    while (offset < len) {
        uint32_t page_addr = addr + offset;
        uint32_t page_offset = page_addr % XSPI_FLASH_PAGE_SIZE;
        uint32_t page_remain = XSPI_FLASH_PAGE_SIZE - page_offset;
        uint32_t chunk = (len - offset < page_remain) ? (len - offset) : page_remain;

        /* Write Enable */
        err = xspi_flash_write_enable(hxspi_flash);
        if (err != XSPI_FLASH_OK) {
            return err;
        }

        /* Page Program (使用 1-line 模式确保兼容性) */
        XSPI_RegularCmdTypeDef cmd = {0};
        cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
        cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
        cmd.Instruction        = W25Q_CMD_PP;
        cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
        cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
        cmd.Address            = page_addr;
        cmd.AddressMode        = HAL_XSPI_ADDRESS_1_LINE;
        cmd.AddressSize        = HAL_XSPI_ADDRESS_32_BITS;
        cmd.DataMode           = HAL_XSPI_DATA_1_LINE;
        cmd.NbData             = chunk;
        cmd.DummyCycles        = 0;

        if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
            hxspi_flash->stats.err_count++;
            return XSPI_FLASH_ERR_COMM;
        }

        if (HAL_XSPI_Transmit(hxspi_flash->hxspi, (uint8_t *)buf + offset,
                               HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
            hxspi_flash->stats.err_count++;
            return XSPI_FLASH_ERR_COMM;
        }

        /* 等待写入完成 */
        err = xspi_flash_wait_wip(hxspi_flash, XSPI_FLASH_WRITE_PAGE_MS + 10);
        if (err != XSPI_FLASH_OK) {
            return err;
        }

        offset += chunk;
    }

    hxspi_flash->stats.write_count++;
    return XSPI_FLASH_OK;
}

/**
 * @brief  擦除扇区 (4KB)
 */
xspi_flash_err_t xspi_flash_erase_sector(xspi_flash_handle_t *hxspi_flash, uint32_t addr)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_CHECK_INIT(hxspi_flash);

    if (addr % XSPI_FLASH_SECTOR_SIZE != 0) {
        return XSPI_FLASH_ERR_INVALID_PARAM;
    }

    xspi_flash_err_t err = xspi_flash_write_enable(hxspi_flash);
    if (err != XSPI_FLASH_OK) {
        return err;
    }

    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_SE;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.Address            = addr;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_1_LINE;
    cmd.AddressSize        = HAL_XSPI_ADDRESS_32_BITS;
    cmd.DataMode           = HAL_XSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    err = xspi_flash_wait_wip(hxspi_flash, XSPI_FLASH_ERASE_SECTOR_MS);
    if (err == XSPI_FLASH_OK) {
        hxspi_flash->stats.erase_sector_count++;
    }

    return err;
}

/**
 * @brief  擦除块 (64KB)
 */
xspi_flash_err_t xspi_flash_erase_block(xspi_flash_handle_t *hxspi_flash, uint32_t addr)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_CHECK_INIT(hxspi_flash);

    if (addr % XSPI_FLASH_BLOCK_SIZE != 0) {
        return XSPI_FLASH_ERR_INVALID_PARAM;
    }

    xspi_flash_err_t err = xspi_flash_write_enable(hxspi_flash);
    if (err != XSPI_FLASH_OK) {
        return err;
    }

    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_BE;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.Address            = addr;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_1_LINE;
    cmd.AddressSize        = HAL_XSPI_ADDRESS_32_BITS;
    cmd.DataMode           = HAL_XSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    err = xspi_flash_wait_wip(hxspi_flash, XSPI_FLASH_ERASE_BLOCK_MS);
    if (err == XSPI_FLASH_OK) {
        hxspi_flash->stats.erase_block_count++;
    }

    return err;
}

/**
 * @brief  整片擦除
 */
xspi_flash_err_t xspi_flash_erase_chip(xspi_flash_handle_t *hxspi_flash)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_CHECK_INIT(hxspi_flash);

    xspi_flash_err_t err = xspi_flash_write_enable(hxspi_flash);
    if (err != XSPI_FLASH_OK) {
        return err;
    }

    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_CE;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_XSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    err = xspi_flash_wait_wip(hxspi_flash, XSPI_FLASH_ERASE_CHIP_MS);
    if (err == XSPI_FLASH_OK) {
        hxspi_flash->stats.erase_chip_count++;
    }

    return err;
}

/**
 * @brief  启用 XIP (Execute In Place) 内存映射模式
 */
xspi_flash_err_t xspi_flash_enable_xip(xspi_flash_handle_t *hxspi_flash)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_CHECK_INIT(hxspi_flash);

    /* 配置内存映射模式 */
    XSPI_MemoryMappedTypeDef mmap_cfg = {0};
    mmap_cfg.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
    mmap_cfg.TimeOutPeriod     = 0;

    /* 配置读取命令为 OctoSPI DTR 快速读取 */
    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_READ_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_8READ_DTR;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDtrMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    cmd.AddressSize        = HAL_XSPI_ADDRESS_32_BITS;
    cmd.AddressDtrMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    cmd.DataMode           = HAL_XSPI_DATA_8_LINES;
    cmd.DataDtrMode        = HAL_XSPI_DATA_DTR_ENABLE;
    cmd.DummyCycles        = XSPI1_DUMMY_CYCLES_DTR;
    cmd.DQSMode            = HAL_XSPI_DQS_ENABLE;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_XIP;
    }

    if (HAL_XSPI_MemoryMapped(hxspi_flash->hxspi, &mmap_cfg) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_XIP;
    }

    hxspi_flash->state = XSPI_FLASH_STATE_XIP;
    hxspi_flash->xip_enabled = 1;
    hxspi_flash->stats.xip_enter_count++;

    return XSPI_FLASH_OK;
}

/**
 * @brief  禁用 XIP 模式
 */
xspi_flash_err_t xspi_flash_disable_xip(xspi_flash_handle_t *hxspi_flash)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);

    if (hxspi_flash->state != XSPI_FLASH_STATE_XIP) {
        return XSPI_FLASH_OK;
    }

    if (HAL_XSPI_Abort(hxspi_flash->hxspi) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_XIP;
    }

    hxspi_flash->state = XSPI_FLASH_STATE_READY;
    hxspi_flash->xip_enabled = 0;
    hxspi_flash->stats.xip_exit_count++;

    return XSPI_FLASH_OK;
}

/**
 * @brief  获取驱动状态
 */
xspi_flash_state_t xspi_flash_get_state(xspi_flash_handle_t *hxspi_flash)
{
    if (hxspi_flash == NULL) {
        return XSPI_FLASH_STATE_RESET;
    }
    return hxspi_flash->state;
}

/**
 * @brief  获取运行统计
 */
xspi_flash_err_t xspi_flash_get_stats(xspi_flash_handle_t *hxspi_flash, xspi_flash_stats_t *stats)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);
    XSPI_FLASH_ASSERT(stats != NULL);

    memcpy(stats, &hxspi_flash->stats, sizeof(xspi_flash_stats_t));
    return XSPI_FLASH_OK;
}

/**
 * @brief  复位 Flash 设备
 */
xspi_flash_err_t xspi_flash_reset_device(xspi_flash_handle_t *hxspi_flash)
{
    XSPI_FLASH_CHECK_HANDLE(hxspi_flash);

    /* Enable Reset */
    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_EN_RST;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_XSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return XSPI_FLASH_ERR_COMM;
    }

    /* Reset Device */
    cmd.Instruction = W25Q_CMD_RST_DEV;
    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return XSPI_FLASH_ERR_COMM;
    }

    HAL_Delay(2); /* 等待复位完成 */

    return XSPI_FLASH_OK;
}

/**
 * @brief  获取错误码描述字符串
 */
const char* xspi_flash_err_to_string(xspi_flash_err_t err)
{
    int idx = -err;
    if (idx >= 0 && idx < (int)(sizeof(s_err_strings) / sizeof(s_err_strings[0]))) {
        return s_err_strings[idx];
    }
    return "Unknown error";
}

/* =============================================================================
 * HAL 回调函数 (弱定义)
 * ==========================================================================*/

__weak void xspi_flash_hal_tc_callback(XSPI_HandleTypeDef *hxspi)
{
    (void)hxspi;
}

__weak void xspi_flash_hal_err_callback(XSPI_HandleTypeDef *hxspi)
{
    (void)hxspi;
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

/**
 * @brief  GPIO 初始化
 */
static xspi_flash_err_t xspi_flash_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = XSPI1_GPIO_AF;

    /* CLK: PB2 */
    GPIO_InitStruct.Pin = XSPI1_CLK_PIN;
    HAL_GPIO_Init(XSPI1_GPIO_PORT_CLK, &GPIO_InitStruct);

    /* D0-D1: PB0-PB1 */
    GPIO_InitStruct.Pin = XSPI1_D0_PIN | XSPI1_D1_PIN;
    HAL_GPIO_Init(XSPI1_GPIO_PORT_D0D1, &GPIO_InitStruct);

    /* D2-D5: PC13, PC2-PC4 */
    GPIO_InitStruct.Pin = XSPI1_D2_PIN | XSPI1_D3_PIN | XSPI1_D4_PIN | XSPI1_D5_PIN;
    HAL_GPIO_Init(XSPI1_GPIO_PORT_D2D5, &GPIO_InitStruct);

    /* D6-D7: PD11-PD12 */
    GPIO_InitStruct.Pin = XSPI1_D6_PIN | XSPI1_D7_PIN;
    HAL_GPIO_Init(XSPI1_GPIO_PORT_D6D7, &GPIO_InitStruct);

    /* NCS: PB6 */
    GPIO_InitStruct.Pin = XSPI1_NCS_PIN;
    HAL_GPIO_Init(XSPI1_GPIO_PORT_NCS, &GPIO_InitStruct);

    return XSPI_FLASH_OK;
}

/**
 * @brief  时钟初始化
 */
static xspi_flash_err_t xspi_flash_clock_init(void)
{
    __HAL_RCC_XSPI1_CLK_ENABLE();
    __HAL_RCC_XSPI1_FORCE_RESET();
    __HAL_RCC_XSPI1_RELEASE_RESET();

    return XSPI_FLASH_OK;
}

/**
 * @brief  HAL 层初始化
 */
static xspi_flash_err_t xspi_flash_hal_init(xspi_flash_handle_t *hxspi_flash)
{
    XSPI_HandleTypeDef *hxspi = hxspi_flash->hxspi;

    hxspi->Instance = XSPI1;

    hxspi->Init.FifoThresholdByte      = 4;
    hxspi->Init.MemoryMode             = HAL_XSPI_SINGLE_MEM;
    hxspi->Init.MemoryType             = HAL_XSPI_MEMTYPE_MACRONIX;
    hxspi->Init.MemorySize             = 23;  /* 2^23 = 8MB, 实际64MB需调整 */
    hxspi->Init.ChipSelectHighTimeCycle  = 2;
    hxspi->Init.FreeRunningClock        = HAL_XSPI_FREERUNCLK_DISABLE;
    hxspi->Init.ClockMode               = HAL_XSPI_CLOCK_MODE_0;
    hxspi->Init.WrapSize               = HAL_XSPI_WRAP_NOT_SUPPORTED;
    hxspi->Init.ClockPrescaler          = 2;    /* HCLK/3 ≈ 266MHz, 实际需按800MHz调整 */
    hxspi->Init.SampleShifting          = HAL_XSPI_SAMPLE_SHIFT_NONE;
    hxspi->Init.DelayHoldQuarterCycle   = HAL_XSPI_DHQC_DISABLE;
    hxspi->Init.ChipSelectBoundary      = HAL_XSPI_CHIP_SELECT_BOUNDARY_NONE;
    hxspi->Init.DelayBlockBypass        = HAL_XSPI_DELAY_BLOCK_BYPASS;
    hxspi->Init.MaxTran                 = 0;
    hxspi->Init.Refresh                 = 0;

    if (HAL_XSPI_Init(hxspi) != HAL_OK) {
        return XSPI_FLASH_ERR_HW_INIT;
    }

    return XSPI_FLASH_OK;
}

/**
 * @brief  等待 Flash 就绪 (WIP=0)
 */
static xspi_flash_err_t xspi_flash_wait_wip(xspi_flash_handle_t *hxspi_flash, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t sr = 0;

    do {
        xspi_flash_err_t err = xspi_flash_read_sr1(hxspi_flash, &sr);
        if (err != XSPI_FLASH_OK) {
            return err;
        }

        if ((sr & W25Q_SR1_WIP) == 0) {
            return XSPI_FLASH_OK;
        }

    } while ((HAL_GetTick() - start) < timeout_ms);

    return XSPI_FLASH_ERR_TIMEOUT;
}

/**
 * @brief  写使能
 */
static xspi_flash_err_t xspi_flash_write_enable(xspi_flash_handle_t *hxspi_flash)
{
    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_WREN;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_XSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    return XSPI_FLASH_OK;
}

/**
 * @brief  写禁用
 */
static xspi_flash_err_t xspi_flash_write_disable(xspi_flash_handle_t *hxspi_flash)
{
    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_WRDI;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_XSPI_DATA_NONE;
    cmd.DummyCycles        = 0;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        hxspi_flash->stats.err_count++;
        return XSPI_FLASH_ERR_COMM;
    }

    return XSPI_FLASH_OK;
}

/**
 * @brief  读取状态寄存器1
 */
static xspi_flash_err_t xspi_flash_read_sr1(xspi_flash_handle_t *hxspi_flash, uint8_t *sr)
{
    XSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId            = HAL_XSPI_FLASH_ID_1;
    cmd.Instruction        = W25Q_CMD_RDSR1;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize    = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    cmd.DataMode           = HAL_XSPI_DATA_1_LINE;
    cmd.NbData             = 1;
    cmd.DummyCycles        = 0;

    if (HAL_XSPI_Command(hxspi_flash->hxspi, &cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return XSPI_FLASH_ERR_COMM;
    }

    if (HAL_XSPI_Receive(hxspi_flash->hxspi, sr, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return XSPI_FLASH_ERR_COMM;
    }

    return XSPI_FLASH_OK;
}

/**
 * @brief  自动擦除 (根据地址和长度)
 */
static xspi_flash_err_t xspi_flash_auto_erase(xspi_flash_handle_t *hxspi_flash, uint32_t addr, uint32_t len)
{
    uint32_t start_sector = addr / XSPI_FLASH_SECTOR_SIZE;
    uint32_t end_sector = (addr + len - 1) / XSPI_FLASH_SECTOR_SIZE;

    for (uint32_t sector = start_sector; sector <= end_sector; sector++) {
        xspi_flash_err_t err = xspi_flash_erase_sector(hxspi_flash, sector * XSPI_FLASH_SECTOR_SIZE);
        if (err != XSPI_FLASH_OK) {
            return err;
        }
    }

    return XSPI_FLASH_OK;
}
