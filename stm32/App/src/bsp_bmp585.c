/**
  ******************************************************************************
  * @file    bsp_bmp585.c
  * @brief   BMP585 气压传感器 SPI2 驱动实现
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - SPI2 @ 10MHz max
  * - 支持软件位、强制/正常测量模式
  ******************************************************************************
  */

#include "bsp_bmp585.h"

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static SPI_HandleTypeDef hbmp585_spi;
static volatile uint8_t s_bmp585_initialized = 0;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void bmp585_spi_init(void);
static void bmp585_cs_low(void);
static void bmp585_cs_high(void);
static uint8_t bmp585_xfer_byte(uint8_t tx);
static lingxi_err_t bmp585_read_reg(uint8_t reg, uint8_t *buf, uint16_t len);
static lingxi_err_t bmp585_write_reg(uint8_t reg, const uint8_t *buf, uint16_t len);

/* =============================================================================
 * 函数实现
 * ==========================================================================*/

/**
 * @brief  初始化 BMP585
 */
lingxi_err_t bsp_bmp585_init(void)
{
    uint8_t chip_id = 0;
    uint8_t buf[2];

    /* SPI 初始化 */
    bmp585_spi_init();

    /* 读取 Chip ID */
    if (bsp_bmp585_read_chip_id(&chip_id) != LINGXI_OK) {
        LX_ERR_PRINT("BMP585 read chip ID failed");
        return LINGXI_ERR_SENSOR;
    }
    if (chip_id != BMP585_CHIP_ID) {
        LX_ERR_PRINT("BMP585 chip ID mismatch: expected 0x%02X, got 0x%02X",
                     BMP585_CHIP_ID, chip_id);
        return LINGXI_ERR_SENSOR;
    }

    /* 软复位 */
    if (bsp_bmp585_soft_reset() != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }

    /* 配置 OSR: 气压 x8, 温度 x1 (平衡精度与功耗) */
    buf[0] = 0x03;  /* osr_p=8, osr_t=1 */
    if (bmp585_write_reg(BMP585_REG_OSR_CONFIG, buf, 1) != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }

    /* 配置 ODR: 50 Hz */
    buf[0] = 0x02;  /* odr=50Hz */
    if (bmp585_write_reg(BMP585_REG_ODR_CONFIG, buf, 1) != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }

    /* 配置 IIR 滤波: 气压 ×8, 温度 ×1 */
    buf[0] = 0x03;  /* iir_p=3 (×8), iir_t=0 (×1) */
    if (bmp585_write_reg(BMP585_REG_DSP_IIR, buf, 1) != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }

    /* 配置 INT: 数据就绪中断 */
    buf[0] = 0x01;  /* drdy_en=1 */
    if (bmp585_write_reg(BMP585_REG_INT_CONFIG, buf, 1) != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }

    /* 进入正常模式 */
    if (bsp_bmp585_set_mode(BMP585_MODE_NORMAL) != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }

    s_bmp585_initialized = 1;
    LX_DEBUG_PRINT("BMP585 initialized, chip_id=0x%02X", chip_id);
    return LINGXI_OK;
}

/**
 * @brief  软复位
 */
lingxi_err_t bsp_bmp585_soft_reset(void)
{
    uint8_t cmd = 0xB6;
    if (bmp585_write_reg(BMP585_REG_CMD, &cmd, 1) != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }
    HAL_Delay(2);  /* 等待复位完成 */
    return LINGXI_OK;
}

/**
 * @brief  读取 Chip ID
 */
lingxi_err_t bsp_bmp585_read_chip_id(uint8_t *chip_id)
{
    LX_RETURN_IF_NULL(chip_id);
    return bmp585_read_reg(BMP585_REG_CHIP_ID, chip_id, 1);
}

/**
 * @brief  设置测量模式
 */
lingxi_err_t bsp_bmp585_set_mode(uint8_t mode)
{
    uint8_t buf[1];
    /* 读取当前 PWR_CTRL */
    if (bmp585_read_reg(BMP585_REG_PWR_CTRL, buf, 1) != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }
    /* 清除 mode 位并设置新模式 */
    buf[0] = (buf[0] & ~0x03) | (mode & 0x03);
    if (bmp585_write_reg(BMP585_REG_PWR_CTRL, buf, 1) != LINGXI_OK) {
        return LINGXI_ERR_SENSOR;
    }
    return LINGXI_OK;
}

/**
 * @brief  读取压力和温度数据
 */
lingxi_err_t bsp_bmp585_read_data(bmp585_data_t *data)
{
    LX_RETURN_IF_NULL(data);

    if (!s_bmp585_initialized) {
        return LINGXI_ERR_NOT_INIT;
    }

    uint8_t raw[6];
    if (bmp585_read_reg(BMP585_REG_PRESS_DATA_XLSB, raw, 6) != LINGXI_OK) {
        return LINGXI_ERR_IO;
    }

    /* 24-bit 压力数据 (有符号) */
    int32_t press_raw = ((int32_t)raw[2] << 16) | ((int32_t)raw[1] << 8) | raw[0];
    if (press_raw & 0x800000) {
        press_raw |= 0xFF000000;
    }

    /* 24-bit 温度数据 (有符号) */
    int32_t temp_raw = ((int32_t)raw[5] << 16) | ((int32_t)raw[4] << 8) | raw[3];
    if (temp_raw & 0x800000) {
        temp_raw |= 0xFF000000;
    }

    /* 转换为物理值 (简化: 假设默认校准参数) */
    /* 气压: LSB / 2^16 * 100 hPa → 实际需要根据校准数据计算 */
    data->pressure_hpa = (float)press_raw / 65536.0f * 100.0f;
    data->temperature_c = (float)temp_raw / 65536.0f * 100.0f;
    data->timestamp_ms = HAL_GetTick();

    return LINGXI_OK;
}

/**
 * @brief  INT 中断处理
 */
void bsp_bmp585_irq_handler(void)
{
    /* 可通知 FreeRTOS 任务进行数据读取 */
    /* 清除中断标志在数据读取时自动完成 */
}

/**
 * @brief  检查数据就绪
 */
bool bsp_bmp585_is_data_ready(void)
{
    if (!s_bmp585_initialized) {
        return false;
    }
    return (HAL_GPIO_ReadPin(BMP585_INT_GPIO_PORT, BMP585_INT_PIN) == GPIO_PIN_SET);
}

/* =============================================================================
 * 私有函数实现
 * ==========================================================================*/

static void bmp585_spi_init(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_SPI2_FORCE_RESET();
    __HAL_RCC_SPI2_RELEASE_RESET();

    hbmp585_spi.Instance = BMP585_SPI;
    hbmp585_spi.Init.Mode = SPI_MODE_MASTER;
    hbmp585_spi.Init.Direction = SPI_DIRECTION_2LINES;
    hbmp585_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    hbmp585_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    hbmp585_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    hbmp585_spi.Init.NSS = SPI_NSS_SOFT;
    hbmp585_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;  /* 200MHz/8 = 25MHz */
    hbmp585_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hbmp585_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    hbmp585_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hbmp585_spi.Init.CRCPolynomial = 7;

    HAL_SPI_Init(&hbmp585_spi);
}

static void bmp585_cs_low(void)
{
    HAL_GPIO_WritePin(BMP585_SPI_GPIO_PORT, BMP585_SPI_CS_PIN, GPIO_PIN_RESET);
}

static void bmp585_cs_high(void)
{
    HAL_GPIO_WritePin(BMP585_SPI_GPIO_PORT, BMP585_SPI_CS_PIN, GPIO_PIN_SET);
}

static uint8_t bmp585_xfer_byte(uint8_t tx)
{
    uint8_t rx = 0;
    HAL_SPI_TransmitReceive(&hbmp585_spi, &tx, &rx, 1, BMP585_SPI_TIMEOUT_MS);
    return rx;
}

static lingxi_err_t bmp585_read_reg(uint8_t reg, uint8_t *buf, uint16_t len)
{
    LX_RETURN_IF_NULL(buf);
    if (len == 0) return LINGXI_OK;

    bmp585_cs_low();
    bmp585_xfer_byte(reg | 0x80);  /* 读操作: bit7=1 */
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = bmp585_xfer_byte(0x00);
    }
    bmp585_cs_high();
    return LINGXI_OK;
}

static lingxi_err_t bmp585_write_reg(uint8_t reg, const uint8_t *buf, uint16_t len)
{
    LX_RETURN_IF_NULL(buf);
    if (len == 0) return LINGXI_OK;

    bmp585_cs_low();
    bmp585_xfer_byte(reg & 0x7F);  /* 写操作: bit7=0 */
    for (uint16_t i = 0; i < len; i++) {
        bmp585_xfer_byte(buf[i]);
    }
    bmp585_cs_high();
    return LINGXI_OK;
}
