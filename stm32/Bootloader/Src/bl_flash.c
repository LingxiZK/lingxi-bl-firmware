/*******************************************************************************
 * @file    bl_flash.c
 * @brief   Bootloader Flash 操作 — OctoSPI XIP (W25Q512)
 * @version 1.1.0
 *
 * 修正说明 (v1.1.0):
 *   STM32N657 没有用户可写的 Internal Flash。
 *   所有非易失操作(包括 Info Sector)都通过 XSPI 操作 W25Q512。
 *   OTA 接收缓存使用 RAM。
 ******************************************************************************/

#include "bl_flash.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * W25Q 指令定义
 *===========================================================================*/
#define W25Q_CMD_WREN           0x06
#define W25Q_CMD_SE_4K_4B       0x21
#define W25Q_CMD_BE_64K_4B      0xDC
#define W25Q_CMD_PP_4B          0x12
#define W25Q_CMD_RD_4B          0x13
#define W25Q_CMD_RDSR1          0x05
#define W25Q_CMD_RDID           0x9F

#define XSPI_TIMEOUT_MS         5000

/*============================================================================
 * 局部变量
 *===========================================================================*/
static XSPI_HandleTypeDef hxspi;
static uint32_t s_flash_id = 0;

/*============================================================================
 * 内部函数
 *===========================================================================*/
static bl_err_t xspi_send_cmd(uint32_t instruction, uint32_t addr, uint32_t addr_mode,
                              uint32_t dummy_cycles, uint8_t *data, uint32_t data_len,
                              uint32_t direction);
static bl_err_t xspi_wait_busy(uint32_t timeout_ms);
static bl_err_t xspi_write_enable(void);

/*============================================================================
 * 初始化
 *===========================================================================*/
bl_err_t bl_flash_init(void)
{
    /* STM32N6 没有用户 Internal Flash，无需解锁 */
    /* XSPI 初始化在 bl_xspi_init() 中完成 */
    return BL_OK;
}

void bl_flash_deinit(void)
{
    /* 无需锁定 Internal Flash */
}

/*============================================================================
 * OctoSPI XSPI 初始化
 *===========================================================================*/
bl_err_t bl_xspi_init(void)
{
    /* 仅保留框架，具体配置需要根据 CubeMX 生成的参数调整 */
    /* 此处示例使用 XSPI1，Single SPI 模式 */

    __HAL_RCC_XSPIM_CLK_ENABLE();
    __HAL_RCC_XSPI1_CLK_ENABLE();

    hxspi.Instance = XSPI1;
    hxspi.Init.FifoThresholdByte      = 4;
    hxspi.Init.MemoryMode             = HAL_XSPI_SINGLE_MEM;
    hxspi.Init.MemoryType             = HAL_XSPI_MEMTYPE_MICRON;
    hxspi.Init.MemorySize             = HAL_XSPI_SIZE_64MB;   /* W25Q512 = 64MB */
    hxspi.Init.ChipSelectHighTimeCycle= 2;
    hxspi.Init.FreeRunningClock       = HAL_XSPI_FREERUNCLK_DISABLE;
    hxspi.Init.ClockMode              = HAL_XSPI_CLOCK_MODE_0;
    hxspi.Init.WrapSize               = HAL_XSPI_WRAP_NOT_SUPPORTED;
    hxspi.Init.ClockPrescaler         = 2;  /* 适当分额 */
    hxspi.Init.SampleShifting         = HAL_XSPI_SAMPLE_SHIFT_NONE;
    hxspi.Init.DelayHoldQuarterCycle  = HAL_XSPI_DHQC_DISABLE;
    hxspi.Init.ChipSelectBoundaryTimeCycle = 0;
    hxspi.Init.MaxTran               = 0;
    hxspi.Init.Refresh               = 0;
    hxspi.Init.MemorySelect          = HAL_XSPI_CSSEL_NCS1;

    if (HAL_XSPI_Init(&hxspi) != HAL_OK) {
        return BL_ERR_XSPI;
    }

    /* 读取 Flash ID */
    uint8_t id_buf[3];
    if (xspi_send_cmd(W25Q_CMD_RDID, 0, HAL_XSPI_ADDRESS_NONE, 0,
                      id_buf, 3, HAL_XSPI_DIRECTION_IN) != BL_OK) {
        return BL_ERR_XSPI;
    }
    s_flash_id = (id_buf[0] << 16) | (id_buf[1] << 8) | id_buf[2];

    /* 检查 ID: Winbond W25Q 系列应该是 0xEFxx */
    if (id_buf[0] != 0xEF) {
        /* 非严格检查，仅警告 */
    }

    return BL_OK;
}

/*============================================================================
 * OctoSPI 操作
 *===========================================================================*/
bl_err_t bl_xspi_erase_sector(uint32_t addr)
{
    bl_err_t err = xspi_write_enable();
    if (err != BL_OK) return err;

    err = xspi_send_cmd(W25Q_CMD_SE_4K_4B, addr, HAL_XSPI_ADDRESS_32_BITS, 0,
                        NULL, 0, HAL_XSPI_DIRECTION_NONE);
    if (err != BL_OK) return err;

    return xspi_wait_busy(XSPI_TIMEOUT_MS);
}

bl_err_t bl_xspi_erase_block64k(uint32_t addr)
{
    bl_err_t err = xspi_write_enable();
    if (err != BL_OK) return err;

    err = xspi_send_cmd(W25Q_CMD_BE_64K_4B, addr, HAL_XSPI_ADDRESS_32_BITS, 0,
                        NULL, 0, HAL_XSPI_DIRECTION_NONE);
    if (err != BL_OK) return err;

    return xspi_wait_busy(XSPI_TIMEOUT_MS);
}

bl_err_t bl_xspi_write_page(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > W25Q_PAGE_SIZE) {
        return BL_ERR_INVALID;
    }

    /* 确保不跨页 */
    uint32_t page_offset = addr % W25Q_PAGE_SIZE;
    if ((page_offset + len) > W25Q_PAGE_SIZE) {
        len = W25Q_PAGE_SIZE - page_offset;
    }

    bl_err_t err = xspi_write_enable();
    if (err != BL_OK) return err;

    err = xspi_send_cmd(W25Q_CMD_PP_4B, addr, HAL_XSPI_ADDRESS_32_BITS, 0,
                        (uint8_t*)data, len, HAL_XSPI_DIRECTION_OUT);
    if (err != BL_OK) return err;

    return xspi_wait_busy(XSPI_TIMEOUT_MS);
}

bl_err_t bl_xspi_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    return xspi_send_cmd(W25Q_CMD_RD_4B, addr, HAL_XSPI_ADDRESS_32_BITS, 8,
                         buf, len, HAL_XSPI_DIRECTION_IN);
}

/*============================================================================
 * XSPI 低层封装
 *===========================================================================*/
static bl_err_t xspi_send_cmd(uint32_t instruction, uint32_t addr, uint32_t addr_mode,
                              uint32_t dummy_cycles, uint8_t *data, uint32_t data_len,
                              uint32_t direction)
{
    XSPI_RegularCmdTypeDef cmd = {0};

    cmd.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    cmd.Instruction        = instruction;
    cmd.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    cmd.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
    cmd.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;

    cmd.Address            = addr;
    cmd.AddressMode        = addr_mode;
    cmd.AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
    cmd.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_DISABLE;

    cmd.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    cmd.DataMode           = (data_len > 0) ? HAL_XSPI_DATA_1_LINE : HAL_XSPI_DATA_NONE;
    cmd.DataLength         = data_len;
    cmd.DataDTRMode        = HAL_XSPI_DATA_DTR_DISABLE;
    cmd.DummyCycles        = dummy_cycles;
    cmd.DQSMode            = HAL_XSPI_DQS_DISABLE;
    cmd.SIOOMode           = HAL_XSPI_SIOO_INST_EVERY_CMD;

    if (HAL_XSPI_Command(&hxspi, &cmd, XSPI_TIMEOUT_MS) != HAL_OK) {
        return BL_ERR_XSPI;
    }

    if (data_len > 0) {
        if (direction == HAL_XSPI_DIRECTION_IN) {
            if (HAL_XSPI_Receive(&hxspi, data, XSPI_TIMEOUT_MS) != HAL_OK) {
                return BL_ERR_XSPI;
            }
        } else if (direction == HAL_XSPI_DIRECTION_OUT) {
            if (HAL_XSPI_Transmit(&hxspi, data, XSPI_TIMEOUT_MS) != HAL_OK) {
                return BL_ERR_XSPI;
            }
        }
    }

    return BL_OK;
}

static bl_err_t xspi_wait_busy(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t sr1;

    do {
        if (xspi_send_cmd(W25Q_CMD_RDSR1, 0, HAL_XSPI_ADDRESS_NONE, 0,
                          &sr1, 1, HAL_XSPI_DIRECTION_IN) != BL_OK) {
            return BL_ERR_XSPI;
        }
        if ((sr1 & 0x01) == 0) {
            return BL_OK;
        }
    } while ((HAL_GetTick() - start) < timeout_ms);

    return BL_ERR_TIMEOUT;
}

static bl_err_t xspi_write_enable(void)
{
    return xspi_send_cmd(W25Q_CMD_WREN, 0, HAL_XSPI_ADDRESS_NONE, 0,
                         NULL, 0, HAL_XSPI_DIRECTION_NONE);
}

/*============================================================================
 * Info Sector 管理 (通过 XSPI 操作 W25Q512)
 *===========================================================================*/
bl_err_t bl_info_read(bl_info_sector_t *info)
{
    if (info == NULL) return BL_ERR_INVALID;
    return bl_xspi_read(BL_INFO_SECTOR_ADDR, (uint8_t*)info, sizeof(bl_info_sector_t));
}

bl_err_t bl_info_write(const bl_info_sector_t *info)
{
    if (info == NULL) return BL_ERR_INVALID;

    bl_info_sector_t temp = *info;
    temp.crc32 = bl_info_calc_crc(info);

    /* 擦除 Info Sector 对应的 4 个 4KB sector (16KB 总共) */
    uint32_t addr = BL_INFO_SECTOR_ADDR;
    for (int i = 0; i < (BL_INFO_SECTOR_SIZE / W25Q_SECTOR_SIZE); i++) {
        bl_err_t err = bl_xspi_erase_sector(addr);
        if (err != BL_OK) return err;
        addr += W25Q_SECTOR_SIZE;
    }

    /* 分页写入 */
    uint32_t offset = 0;
    uint8_t *p = (uint8_t*)&temp;
    uint32_t total_len = sizeof(bl_info_sector_t);

    while (offset < total_len) {
        uint16_t chunk = (total_len - offset) > W25Q_PAGE_SIZE ? W25Q_PAGE_SIZE : (total_len - offset);
        bl_err_t err = bl_xspi_write_page(BL_INFO_SECTOR_ADDR + offset, p + offset, chunk);
        if (err != BL_OK) return err;
        offset += chunk;
    }

    return BL_OK;
}

bl_err_t bl_info_init_default(void)
{
    bl_info_sector_t info = {0};
    info.magic = BL_INFO_MAGIC;
    info.struct_version = BL_INFO_VERSION;
    info.active_partition = 0;  /* 默认启动 App A */
    info.boot_count = 0;
    info.crc32 = bl_info_calc_crc(&info);
    return bl_info_write(&info);
}

bool bl_info_validate(const bl_info_sector_t *info)
{
    if (info == NULL) return false;
    if (info->magic != BL_INFO_MAGIC) return false;
    if (info->struct_version != BL_INFO_VERSION) return false;

    uint32_t calc = bl_info_calc_crc(info);
    return (calc == info->crc32);
}

uint32_t bl_info_calc_crc(const bl_info_sector_t *info)
{
    if (info == NULL) return 0;
    /* CRC32 计算范围: 从 magic 开始到 crc32 前 */
    uint32_t len = offsetof(bl_info_sector_t, crc32);
    return bl_crc32((const uint8_t*)info, len);
}

/*============================================================================
 * CRC32 (IEEE 802.3)
 *===========================================================================*/
uint32_t bl_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

/*============================================================================
 * 固件校验
 *===========================================================================*/
bl_err_t bl_verify_firmware(uint32_t addr, uint32_t size, uint32_t expected_crc)
{
    if (size == 0 || size > BL_APP_A_CODE_SIZE) {
        return BL_ERR_INVALID;
    }

    uint32_t calc_crc = 0xFFFFFFFF;
    uint8_t buf[512];
    uint32_t remain = size;
    uint32_t read_addr = addr;

    while (remain > 0) {
        uint32_t chunk = (remain > sizeof(buf)) ? sizeof(buf) : remain;
        bl_err_t err = bl_xspi_read(read_addr, buf, chunk);
        if (err != BL_OK) {
            return err;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            calc_crc ^= buf[i];
            for (uint8_t j = 0; j < 8; j++) {
                calc_crc = (calc_crc >> 1) ^ (0xEDB88320 & -(calc_crc & 1));
            }
        }
        read_addr += chunk;
        remain -= chunk;
    }

    calc_crc = ~calc_crc;
    if (calc_crc != expected_crc) {
        return BL_ERR_VERIFY;
    }
    return BL_OK;
}

/*============================================================================
 * 跳转到 App (XIP)
 *===========================================================================*/
void bl_jump_to_app(uint32_t xip_addr)
{
    /* 检查地址对齐 */
    if ((xip_addr & 0xFF) != 0) {
        xip_addr &= ~0xFF;
    }

    /* 获取 App 的 VTOR 和复位处理函数
     * 注意: xip_addr 是 App 代码起始地址(已跳过镜像头)
     * 向量表在代码起始处，所以直接读 xip_addr 即可
     */
    uint32_t app_msp  = *(__IO uint32_t*)xip_addr;
    uint32_t app_rst  = *(__IO uint32_t*)(xip_addr + 4);

    /* 检查 MSP 是否在有效 RAM 区域 (AXISRAM: 0x24000000 - 0x241FFFFF) */
    if ((app_msp < 0x24000000) || (app_msp > 0x24200000)) {
        return;  /* 无效的 MSP */
    }

    /* 关闭中断 */
    __disable_irq();

    /* 重置周边设备 */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* 关闭 SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 设置 VTOR 到 XIP 地址 */
    SCB->VTOR = xip_addr;

    /* 设置 MSP */
    __set_MSP(app_msp);

    /* 跳转 */
    void (*app_reset_handler)(void) = (void (*)(void))app_rst;
    app_reset_handler();
}
