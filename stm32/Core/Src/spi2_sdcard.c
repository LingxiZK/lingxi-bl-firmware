/**
  ******************************************************************************
  * @file    spi2_sdcard.c
  * @brief   SPI2 microSD HAL 驱动实现 (P2批次)
  * @author  Lingxi Team
  * @version v3.2
  * @date    2026-05-15
  ******************************************************************************
  * @attention
  * - 基于 STM32N6xx HAL 库
  * - SPI2 @ 25MHz max, 软件 CS 控制
  * - 支持 SD/SDHC/SDXC, 配合 FatFs 文件系统
  * - 引脚: PB13(SCK), PB14(MISO), PB15(MOSI), PB12(CS), PD3(CD)
  * - 包含 DMA 传输支持 (MDMA_Channel0 备选)
  ******************************************************************************
  */

#include "spi2_sdcard.h"
#include "stm32n6xx_hal.h"
#include <string.h>

/* 引入 FatFs 类型定义 (如果可用) */
#if defined(_FF_DEFINED)
  #include "ff.h"
  #include "diskio.h"
#else
  /* 最小化 FatFs 类型定义, 供编译通过 */
  typedef unsigned char  BYTE;
  typedef unsigned int   UINT;
  typedef uint32_t       LBA_t;
  typedef uint8_t        DSTATUS;
  typedef uint8_t        DRESULT;
  #define RES_OK         0
  #define RES_ERROR      1
  #define RES_WRPRT      2
  #define RES_NOTRDY     3
  #define RES_PARERR     4
  #define STA_NOINIT      0x01
  #define STA_NODISK      0x02
  #define STA_PROTECT     0x04
  #define CTRL_SYNC       0
  #define GET_SECTOR_COUNT 1
  #define GET_SECTOR_SIZE  2
  #define GET_BLOCK_SIZE   3
  #define CTRL_TRIM       4
#endif

/* =============================================================================
 * SD 命令定义
 * ==========================================================================*/
#define SD_CMD0         0x40    /* GO_IDLE_STATE */
#define SD_CMD1         0x41    /* SEND_OP_COND (MMC) */
#define SD_CMD8         0x48    /* SEND_IF_COND */
#define SD_CMD9         0x49    /* SEND_CSD */
#define SD_CMD10        0x4A    /* SEND_CID */
#define SD_CMD12        0x4C    /* STOP_TRANSMISSION */
#define SD_CMD16        0x50    /* SET_BLOCKLEN */
#define SD_CMD17        0x51    /* READ_SINGLE_BLOCK */
#define SD_CMD18        0x52    /* READ_MULTIPLE_BLOCK */
#define SD_CMD24        0x58    /* WRITE_BLOCK */
#define SD_CMD25        0x59    /* WRITE_MULTIPLE_BLOCK */
#define SD_CMD55        0x77    /* APP_CMD */
#define SD_CMD58        0x7A    /* READ_OCR */
#define SD_ACMD41       0x69    /* SD_SEND_OP_COND */

/* R1 响应位 */
#define SD_R1_IDLE      0x01
#define SD_R1_ERASE_RST 0x02
#define SD_R1_ILLEGAL   0x04
#define SD_R1_CRC_ERR   0x08
#define SD_R1_ERASE_SEQ 0x10
#define SD_R1_ADDR_ERR  0x20
#define SD_R1_PARAM_ERR 0x40

/* 数据令牌 */
#define SD_TOKEN_START          0xFE    /* 单块读取/写入起始 */
#define SD_TOKEN_START_MULTI    0xFC    /* 多块写入起始 */
#define SD_TOKEN_STOP_MULTI     0xFD    /* 多块写入停止 */
#define SD_DATA_ACCEPTED        0x05    /* 数据接收成功 */
#define SD_DATA_CRC_ERR         0x0B    /* CRC 错误 */
#define SD_DATA_WRITE_ERR       0x0D    /* 写入错误 */

/* =============================================================================
 * 私有变量
 * ==========================================================================*/
static SPI_HandleTypeDef hspi2_sd;
static volatile uint8_t s_sd_initialized = 0;
static volatile sd_type_t s_sd_type = SD_TYPE_NONE;
static sd_info_t s_sd_info;
static volatile uint8_t s_sd_dma_busy = 0;

/* DMA 句柄 (使用通用 DMA, MDMA 在 dma_config.c 中统一管理) */
static DMA_HandleTypeDef hdma_spi2_tx;
static DMA_HandleTypeDef hdma_spi2_rx;

/* =============================================================================
 * 私有函数声明
 * ==========================================================================*/
static void     sd_gpio_init(void);
static void     sd_spi_init_low_speed(void);
static void     sd_spi_init_high_speed(void);
static void     sd_dma_init(void);
static void     sd_cs_low(void);
static void     sd_cs_high(void);
static uint8_t  sd_spi_txrx_byte(uint8_t tx);
static void     sd_spi_tx_byte(uint8_t tx);
static uint8_t  sd_spi_rx_byte(void);
static uint8_t  sd_send_cmd(uint8_t cmd, uint32_t arg);
static uint8_t  sd_send_acmd(uint8_t acmd, uint32_t arg);
static sd_err_t sd_wait_ready(uint32_t timeout_ms);
static sd_err_t sd_wait_data_token(uint8_t token, uint32_t timeout_ms);
static sd_err_t sd_read_data(uint8_t *buf, uint16_t len);
static sd_err_t sd_write_data(const uint8_t *buf, uint16_t len, uint8_t token);
static sd_err_t sd_read_csd(void);
static sd_err_t sd_read_cid(void);
static sd_err_t sd_calc_capacity(const uint8_t *csd);
static void     sd_deselect(void);
static void     sd_select(void);

/* =============================================================================
 * GPIO / SPI 初始化
 * ==========================================================================*/

/**
 * @brief  初始化 SD 卡检测和 SPI GPIO
 */
static void sd_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    SD_GPIO_CLK_ENABLE();

    /* SPI2 引脚: PB13(SCK), PB14(MISO), PB15(MOSI) */
    GPIO_InitStruct.Pin       = SD_PIN_SCK | SD_PIN_MISO | SD_PIN_MOSI;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = SD_AF_SPI;
    HAL_GPIO_Init(SD_GPIO_PORT_SPI, &GPIO_InitStruct);

    /* CS: PB12 软件控制 */
    GPIO_InitStruct.Pin       = SD_PIN_CS;
    GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(SD_GPIO_PORT_SPI, &GPIO_InitStruct);
    HAL_GPIO_WritePin(SD_GPIO_PORT_SPI, SD_PIN_CS, GPIO_PIN_SET);

    /* CD: PD3 输入 (带内部上拉, 插入卡后拉低) */
    GPIO_InitStruct.Pin       = SD_PIN_CD;
    GPIO_InitStruct.Mode      = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    HAL_GPIO_Init(SD_GPIO_PORT_CD, &GPIO_InitStruct);
}

/**
 * @brief  初始化 SPI2 低速模式 (400kHz, 用于 SD 卡初始化)
 */
static void sd_spi_init_low_speed(void)
{
    SD_SPI_CLK_ENABLE();
    SD_SPI_FORCE_RESET();
    SD_SPI_RELEASE_RESET();

    hspi2_sd.Instance               = SD_SPI;
    hspi2_sd.Init.Mode              = SPI_MODE_MASTER;
    hspi2_sd.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi2_sd.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi2_sd.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi2_sd.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi2_sd.Init.NSS               = SPI_NSS_SOFT;
    /* HCLK=200MHz, 分频256 -> ~781kHz (安全初始化速度) */
    hspi2_sd.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    hspi2_sd.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi2_sd.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi2_sd.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi2_sd.Init.CRCPolynomial     = 7;
    hspi2_sd.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
    hspi2_sd.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;

    HAL_SPI_Init(&hspi2_sd);
}

/**
 * @brief  切换到 SPI2 高速模式 (25MHz)
 */
static void sd_spi_init_high_speed(void)
{
    HAL_SPI_DeInit(&hspi2_sd);

    /* HCLK=200MHz, 分频8 -> 25MHz */
    hspi2_sd.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;

    HAL_SPI_Init(&hspi2_sd);
}

/**
 * @brief  初始化 SPI2 DMA (可选, 用于批量传输)
 */
static void sd_dma_init(void)
{
    /* DMA 时钟已在 dma_config.c 中统一初始化 */
    /* 此处仅配置 SPI2 TX/RX DMA 句柄参数 */

    hdma_spi2_tx.Instance                 = DMA1_Stream0;  /* 根据实际分配调整 */
    hdma_spi2_tx.Init.Request             = DMA_REQUEST_SPI2_TX;
    hdma_spi2_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_spi2_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi2_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi2_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi2_tx.Init.Mode                = DMA_NORMAL;
    hdma_spi2_tx.Init.Priority            = DMA_PRIORITY_LOW;
    hdma_spi2_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    HAL_DMA_Init(&hdma_spi2_tx);
    __HAL_LINKDMA(&hspi2_sd, hdmatx, hdma_spi2_tx);

    hdma_spi2_rx.Instance                 = DMA1_Stream1;
    hdma_spi2_rx.Init.Request             = DMA_REQUEST_SPI2_RX;
    hdma_spi2_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_spi2_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi2_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi2_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi2_rx.Init.Mode                = DMA_NORMAL;
    hdma_spi2_rx.Init.Priority            = DMA_PRIORITY_LOW;
    hdma_spi2_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    HAL_DMA_Init(&hdma_spi2_rx);
    __HAL_LINKDMA(&hspi2_sd, hdmarx, hdma_spi2_rx);
}

/* =============================================================================
 * 底层 SPI 操作
 * ==========================================================================*/

/**
 * @brief  CS 拉低 (选中 SD 卡)
 */
static void sd_cs_low(void)
{
    HAL_GPIO_WritePin(SD_GPIO_PORT_SPI, SD_PIN_CS, GPIO_PIN_RESET);
}

/**
 * @brief  CS 拉高 (释放 SD 卡)
 */
static void sd_cs_high(void)
{
    HAL_GPIO_WritePin(SD_GPIO_PORT_SPI, SD_PIN_CS, GPIO_PIN_SET);
}

/**
 * @brief  SPI 单字节收发
 */
static uint8_t sd_spi_txrx_byte(uint8_t tx)
{
    uint8_t rx = 0;
    HAL_SPI_TransmitReceive(&hspi2_sd, &tx, &rx, 1, 10);
    return rx;
}

/**
 * @brief  SPI 发送单字节
 */
static void sd_spi_tx_byte(uint8_t tx)
{
    HAL_SPI_Transmit(&hspi2_sd, &tx, 1, 10);
}

/**
 * @brief  SPI 接收单字节 (发送 0xFF 获取响应)
 */
static uint8_t sd_spi_rx_byte(void)
{
    return sd_spi_txrx_byte(0xFF);
}

/**
 * @brief  选中 SD 卡并发送时钟
 */
static void sd_select(void)
{
    sd_cs_low();
    /* 至少 1 字节时钟让 SD 卡进入 SPI 模式 */
    sd_spi_rx_byte();
}

/**
 * @brief  释放 SD 卡
 */
static void sd_deselect(void)
{
    sd_cs_high();
    sd_spi_rx_byte();  /* 额外时钟 */
}

/* =============================================================================
 * SD 命令层
 * ==========================================================================*/

/**
 * @brief  等待 SD 卡就绪 (DO 保持高电平)
 */
static sd_err_t sd_wait_ready(uint32_t timeout_ms)
{
    uint32_t tickstart = HAL_GetTick();
    uint8_t resp;

    do {
        resp = sd_spi_rx_byte();
        if (resp == 0xFF) {
            return SD_OK;
        }
    } while ((HAL_GetTick() - tickstart) < timeout_ms);

    return SD_ERR_TIMEOUT;
}

/**
 * @brief  发送 SD 命令并获取 R1 响应
 * @param  cmd: 命令字节 (已包含起始位 01 和传输位)
 * @param  arg: 32 位参数
 * @retval R1 响应字节
 */
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t buf[6];
    uint8_t r1;
    uint8_t crc = 0x01;  /* 默认 CRC (除 CMD0/CMD8 外被忽略) */

    /* 特殊命令 CRC */
    if (cmd == SD_CMD0) crc = 0x95;
    if (cmd == SD_CMD8) crc = 0x87;

    sd_select();

    /* 等待卡就绪 */
    if (sd_wait_ready(SD_TIMEOUT_MS) != SD_OK) {
        sd_deselect();
        return 0xFF;
    }

    /* 发送命令帧 (6 bytes) */
    buf[0] = cmd;
    buf[1] = (uint8_t)(arg >> 24);
    buf[2] = (uint8_t)(arg >> 16);
    buf[3] = (uint8_t)(arg >> 8);
    buf[4] = (uint8_t)arg;
    buf[5] = crc;

    HAL_SPI_Transmit(&hspi2_sd, buf, 6, 100);

    /* 读取 R1 响应 (跳过前导 0xFF) */
    uint32_t tickstart = HAL_GetTick();
    do {
        r1 = sd_spi_rx_byte();
    } while ((r1 & 0x80) && ((HAL_GetTick() - tickstart) < 500));

    /* 对于 CMD12, 需要额外读取一个忙字节 */
    if (cmd == SD_CMD12) {
        sd_spi_rx_byte();
    }

    sd_deselect();
    return r1;
}

/**
 * @brief  发送 ACMD (APP 命令, CMD55 + ACMD)
 */
static uint8_t sd_send_acmd(uint8_t acmd, uint32_t arg)
{
    uint8_t r1 = sd_send_cmd(SD_CMD55, 0);
    if (r1 > 1) {
        return r1;
    }
    return sd_send_cmd(acmd, arg);
}

/**
 * @brief  等待数据令牌
 */
static sd_err_t sd_wait_data_token(uint8_t token, uint32_t timeout_ms)
{
    uint8_t resp;
    uint32_t tickstart = HAL_GetTick();

    do {
        resp = sd_spi_rx_byte();
        if (resp == token) {
            return SD_OK;
        }
        if (resp != 0xFF) {
            /* 收到错误响应 */
            return SD_ERR_IO;
        }
    } while ((HAL_GetTick() - tickstart) < timeout_ms);

    return SD_ERR_TIMEOUT;
}

/**
 * @brief  读取数据块
 */
static sd_err_t sd_read_data(uint8_t *buf, uint16_t len)
{
    sd_err_t err;

    /* 等待数据起始令牌 0xFE */
    err = sd_wait_data_token(SD_TOKEN_START, SD_TIMEOUT_MS);
    if (err != SD_OK) {
        return err;
    }

    /* 读取数据 */
    HAL_SPI_Receive(&hspi2_sd, buf, len, SD_TIMEOUT_MS);

    /* 读取并丢弃 CRC16 */
    sd_spi_rx_byte();
    sd_spi_rx_byte();

    return SD_OK;
}

/**
 * @brief  写入数据块
 */
static sd_err_t sd_write_data(const uint8_t *buf, uint16_t len, uint8_t token)
{
    uint8_t resp;

    /* 等待卡就绪 */
    if (sd_wait_ready(SD_TIMEOUT_MS) != SD_OK) {
        return SD_ERR_TIMEOUT;
    }

    /* 发送数据起始令牌 */
    sd_spi_tx_byte(token);

    /* 发送数据 */
    HAL_SPI_Transmit(&hspi2_sd, (uint8_t *)buf, len, SD_TIMEOUT_MS);

    /* 发送 CRC16 (dummy) */
    sd_spi_tx_byte(0xFF);
    sd_spi_tx_byte(0xFF);

    /* 读取数据响应 */
    resp = sd_spi_rx_byte();
    if ((resp & 0x1F) != SD_DATA_ACCEPTED) {
        return SD_ERR_IO;
    }

    /* 等待写入完成 (卡忙) */
    return sd_wait_ready(SD_TIMEOUT_MS);
}

/* =============================================================================
 * SD 卡信息读取
 * ==========================================================================*/

/**
 * @brief  读取 CSD 寄存器
 */
static sd_err_t sd_read_csd(void)
{
    uint8_t r1 = sd_send_cmd(SD_CMD9, 0);
    if (r1 != 0x00) {
        return SD_ERR_CMD;
    }

    sd_select();
    sd_err_t err = sd_read_data(s_sd_info.csd, 16);
    sd_deselect();
    return err;
}

/**
 * @brief  读取 CID 寄存器
 */
static sd_err_t sd_read_cid(void)
{
    uint8_t r1 = sd_send_cmd(SD_CMD10, 0);
    if (r1 != 0x00) {
        return SD_ERR_CMD;
    }

    sd_select();
    sd_err_t err = sd_read_data(s_sd_info.cid, 16);
    sd_deselect();
    return err;
}

/**
 * @brief  从 CSD 计算容量
 */
static sd_err_t sd_calc_capacity(const uint8_t *csd)
{
    uint32_t c_size;
    uint32_t capacity_bytes;

    if ((csd[0] >> 6) == 0x01) {
        /* CSD v2.0 (SDHC/SDXC) */
        c_size = ((uint32_t)(csd[7] & 0x3F) << 16) |
                 ((uint32_t)csd[8] << 8) |
                 csd[9];
        /* 容量 = (C_SIZE + 1) * 512KB */
        capacity_bytes = (c_size + 1) * 512ULL * 1024ULL;
    } else {
        /* CSD v1.0 (SD) */
        uint32_t c_size_mult = ((csd[9] & 0x03) << 1) | ((csd[10] & 0x80) >> 7);
        uint32_t read_bl_len = csd[5] & 0x0F;
        c_size = ((uint32_t)((csd[6] & 0x03) << 10) |
                  ((uint32_t)csd[7] << 2) |
                  ((csd[8] & 0xC0) >> 6));
        capacity_bytes = (c_size + 1) * (1UL << (c_size_mult + 2)) * (1UL << read_bl_len);
    }

    s_sd_info.capacity_mb = capacity_bytes / (1024 * 1024);
    s_sd_info.block_count = capacity_bytes / SD_BLOCK_SIZE;
    s_sd_info.block_size  = SD_BLOCK_SIZE;

    return SD_OK;
}

/* =============================================================================
 * 公共 API 实现
 * ==========================================================================*/

/**
 * @brief  初始化 SPI2 SD 卡
 * @retval SD_OK 成功
 */
sd_err_t SD_Init(void)
{
    uint8_t r1;
    uint32_t tickstart;
    uint8_t ocr[4];

    /* 如果已初始化, 先反初始化 */
    if (s_sd_initialized) {
        SD_DeInit();
    }

    /* 检查卡是否插入 */
    if (!SD_IsPresent()) {
        return SD_ERR_NOT_PRESENT;
    }

    /* GPIO 和 SPI 低速初始化 */
    sd_gpio_init();
    sd_spi_init_low_speed();

    /* 发送至少 74 个时钟 (10 字节 0xFF) 让 SD 卡进入 SPI 模式 */
    sd_cs_high();
    for (uint8_t i = 0; i < 10; i++) {
        sd_spi_tx_byte(0xFF);
    }

    /* CMD0: GO_IDLE_STATE */
    uint8_t retries = SD_INIT_RETRIES;
    do {
        r1 = sd_send_cmd(SD_CMD0, 0);
        retries--;
    } while (r1 != SD_R1_IDLE && retries > 0);

    if (r1 != SD_R1_IDLE) {
        return SD_ERR_INIT;
    }

    /* CMD8: SEND_IF_COND (检测 SD v2.0+) */
    r1 = sd_send_cmd(SD_CMD8, 0x1AA);
    if (r1 == SD_R1_IDLE) {
        /* SD v2.0+ */
        sd_select();
        ocr[0] = sd_spi_rx_byte();
        ocr[1] = sd_spi_rx_byte();
        ocr[2] = sd_spi_rx_byte();
        ocr[3] = sd_spi_rx_byte();
        sd_deselect();

        if (ocr[3] != 0xAA) {
            return SD_ERR_INIT;
        }

        /* ACMD41 with HCS bit (支持 SDHC/SDXC) */
        tickstart = HAL_GetTick();
        do {
            r1 = sd_send_acmd(SD_ACMD41, 0x40000000);
        } while (r1 == SD_R1_IDLE && ((HAL_GetTick() - tickstart) < 1000));

        if (r1 != 0x00) {
            return SD_ERR_TIMEOUT;
        }

        /* CMD58: READ_OCR 确认 CCS 位 */
        r1 = sd_send_cmd(SD_CMD58, 0);
        if (r1 != 0x00) {
            return SD_ERR_CMD;
        }

        sd_select();
        ocr[0] = sd_spi_rx_byte();
        ocr[1] = sd_spi_rx_byte();
        ocr[2] = sd_spi_rx_byte();
        ocr[3] = sd_spi_rx_byte();
        sd_deselect();

        memcpy(s_sd_info.ocr, ocr, 4);

        /* OCR[30] = CCS: 1=SDHC/SDXC, 0=SD */
        if (ocr[0] & 0x40) {
            s_sd_type = SD_TYPE_V2HC;
        } else {
            s_sd_type = SD_TYPE_V2;
        }
    } else {
        /* SD v1.x 或 MMC */
        /* 先尝试 ACMD41 (SD) */
        tickstart = HAL_GetTick();
        do {
            r1 = sd_send_acmd(SD_ACMD41, 0);
        } while (r1 == SD_R1_IDLE && ((HAL_GetTick() - tickstart) < 1000));

        if (r1 == 0x00) {
            s_sd_type = SD_TYPE_V1;
        } else {
            /* 尝试 CMD1 (MMC) */
            tickstart = HAL_GetTick();
            do {
                r1 = sd_send_cmd(SD_CMD1, 0);
            } while (r1 == SD_R1_IDLE && ((HAL_GetTick() - tickstart) < 1000));

            if (r1 != 0x00) {
                return SD_ERR_INIT;
            }
            s_sd_type = SD_TYPE_MMC;
        }
    }

    /* CMD16: SET_BLOCKLEN = 512 (对 SDHC/SDXC 可选但推荐) */
    if (s_sd_type != SD_TYPE_V2HC && s_sd_type != SD_TYPE_V2XC) {
        r1 = sd_send_cmd(SD_CMD16, SD_BLOCK_SIZE);
        if (r1 != 0x00) {
            return SD_ERR_CMD;
        }
    }

    /* 读取 CSD 和 CID */
    if (sd_read_csd() != SD_OK) {
        return SD_ERR_IO;
    }
    sd_read_cid();  /* CID 读取失败不影响功能 */

    /* 计算容量 */
    sd_calc_capacity(s_sd_info.csd);
    s_sd_info.type = s_sd_type;

    /* 切换到高速模式 */
    sd_spi_init_high_speed();

    /* 可选: 初始化 DMA */
    /* sd_dma_init(); */

    s_sd_initialized = 1;
    return SD_OK;
}

/**
 * @brief  反初始化 SPI2 SD 接口
 */
sd_err_t SD_DeInit(void)
{
    s_sd_initialized = 0;
    s_sd_type = SD_TYPE_NONE;
    memset(&s_sd_info, 0, sizeof(s_sd_info));

    HAL_SPI_DeInit(&hspi2_sd);
    SD_SPI_CLK_DISABLE();

    /* GPIO 恢复输入模式 (可选) */
    HAL_GPIO_DeInit(SD_GPIO_PORT_SPI, SD_PIN_SCK | SD_PIN_MISO | SD_PIN_MOSI | SD_PIN_CS);
    HAL_GPIO_DeInit(SD_GPIO_PORT_CD, SD_PIN_CD);

    return SD_OK;
}

/**
 * @brief  检测 SD 卡是否插入
 * @note   CD 引脚低电平 = 已插入 (内部上拉, 卡座接地)
 */
bool SD_IsPresent(void)
{
    return (HAL_GPIO_ReadPin(SD_GPIO_PORT_CD, SD_PIN_CD) == GPIO_PIN_RESET);
}

/**
 * @brief  获取卡检测引脚原始状态
 */
GPIO_PinState SD_GetCardDetectPinState(void)
{
    return HAL_GPIO_ReadPin(SD_GPIO_PORT_CD, SD_PIN_CD);
}

/**
 * @brief  获取 SD 卡信息
 */
sd_err_t SD_GetInfo(sd_info_t *info)
{
    if (info == NULL) {
        return SD_ERR_IO;
    }
    if (!s_sd_initialized) {
        return SD_ERR_INIT;
    }
    memcpy(info, &s_sd_info, sizeof(sd_info_t));
    return SD_OK;
}

/**
 * @brief  读取单个块 (512 bytes)
 */
sd_err_t SD_ReadBlock(uint32_t block_addr, uint8_t *buf)
{
    uint8_t r1;
    uint32_t addr;

    if (buf == NULL || !s_sd_initialized) {
        return SD_ERR_INIT;
    }

    /* SDHC/SDXC 使用块地址, SD v1/v2 使用字节地址 */
    if (s_sd_type == SD_TYPE_V2HC || s_sd_type == SD_TYPE_V2XC) {
        addr = block_addr;
    } else {
        addr = block_addr * SD_BLOCK_SIZE;
    }

    /* CMD17: READ_SINGLE_BLOCK */
    r1 = sd_send_cmd(SD_CMD17, addr);
    if (r1 != 0x00) {
        return SD_ERR_CMD;
    }

    sd_select();
    sd_err_t err = sd_read_data(buf, SD_BLOCK_SIZE);
    sd_deselect();

    return err;
}

/**
 * @brief  写入单个块 (512 bytes)
 */
sd_err_t SD_WriteBlock(uint32_t block_addr, const uint8_t *buf)
{
    uint8_t r1;
    uint32_t addr;

    if (buf == NULL || !s_sd_initialized) {
        return SD_ERR_INIT;
    }

    if (s_sd_type == SD_TYPE_V2HC || s_sd_type == SD_TYPE_V2XC) {
        addr = block_addr;
    } else {
        addr = block_addr * SD_BLOCK_SIZE;
    }

    /* CMD24: WRITE_BLOCK */
    r1 = sd_send_cmd(SD_CMD24, addr);
    if (r1 != 0x00) {
        return SD_ERR_CMD;
    }

    sd_select();
    sd_err_t err = sd_write_data(buf, SD_BLOCK_SIZE, SD_TOKEN_START);
    sd_deselect();

    return err;
}

/**
 * @brief  读取多个连续块
 */
sd_err_t SD_ReadBlocks(uint32_t block_addr, uint8_t *buf, uint32_t num_blocks)
{
    uint8_t r1;
    uint32_t addr;
    sd_err_t err = SD_OK;

    if (buf == NULL || !s_sd_initialized || num_blocks == 0) {
        return SD_ERR_INIT;
    }

    if (s_sd_type == SD_TYPE_V2HC || s_sd_type == SD_TYPE_V2XC) {
        addr = block_addr;
    } else {
        addr = block_addr * SD_BLOCK_SIZE;
    }

    /* CMD18: READ_MULTIPLE_BLOCK */
    r1 = sd_send_cmd(SD_CMD18, addr);
    if (r1 != 0x00) {
        return SD_ERR_CMD;
    }

    sd_select();
    for (uint32_t i = 0; i < num_blocks; i++) {
        err = sd_read_data(buf + (i * SD_BLOCK_SIZE), SD_BLOCK_SIZE);
        if (err != SD_OK) {
            break;
        }
    }
    sd_deselect();

    /* CMD12: STOP_TRANSMISSION */
    sd_send_cmd(SD_CMD12, 0);

    return err;
}

/**
 * @brief  写入多个连续块
 */
sd_err_t SD_WriteBlocks(uint32_t block_addr, const uint8_t *buf, uint32_t num_blocks)
{
    uint8_t r1;
    uint32_t addr;
    sd_err_t err = SD_OK;

    if (buf == NULL || !s_sd_initialized || num_blocks == 0) {
        return SD_ERR_INIT;
    }

    if (s_sd_type == SD_TYPE_V2HC || s_sd_type == SD_TYPE_V2XC) {
        addr = block_addr;
    } else {
        addr = block_addr * SD_BLOCK_SIZE;
    }

    /* CMD25: WRITE_MULTIPLE_BLOCK */
    r1 = sd_send_cmd(SD_CMD25, addr);
    if (r1 != 0x00) {
        return SD_ERR_CMD;
    }

    sd_select();
    for (uint32_t i = 0; i < num_blocks; i++) {
        err = sd_write_data(buf + (i * SD_BLOCK_SIZE), SD_BLOCK_SIZE,
                            (i == 0) ? SD_TOKEN_START_MULTI : SD_TOKEN_START_MULTI);
        if (err != SD_OK) {
            break;
        }
    }

    /* 发送停止令牌 */
    sd_spi_tx_byte(SD_TOKEN_STOP_MULTI);
    sd_wait_ready(SD_TIMEOUT_MS);
    sd_deselect();

    /* CMD12: STOP_TRANSMISSION */
    sd_send_cmd(SD_CMD12, 0);

    return err;
}

/**
 * @brief  获取 SD 卡状态
 */
sd_err_t SD_GetStatus(void)
{
    if (!s_sd_initialized) {
        return SD_ERR_INIT;
    }
    if (!SD_IsPresent()) {
        return SD_ERR_NOT_PRESENT;
    }

    /* CMD13: SEND_STATUS */
    uint8_t r1 = sd_send_cmd(0x4D, 0);
    if (r1 != 0x00) {
        return SD_ERR_CMD;
    }

    /* 读取第二个状态字节 */
    uint8_t r2 = sd_spi_rx_byte();
    if (r2 & 0x01) {
        return SD_ERR_IO;  /* 卡处于空闲状态 */
    }

    return SD_OK;
}

/* =============================================================================
 * FatFs 磁盘 I/O 接口实现
 * ==========================================================================*/

/**
 * @brief  FatFs 磁盘初始化
 */
DSTATUS SD_disk_initialize(BYTE pdrv)
{
    (void)pdrv;

    if (!SD_IsPresent()) {
        return STA_NODISK;
    }

    if (SD_Init() == SD_OK) {
        return RES_OK;
    }

    return STA_NOINIT;
}

/**
 * @brief  FatFs 磁盘状态查询
 */
DSTATUS SD_disk_status(BYTE pdrv)
{
    (void)pdrv;

    if (!SD_IsPresent()) {
        return STA_NODISK;
    }

    if (!s_sd_initialized) {
        return STA_NOINIT;
    }

    return RES_OK;
}

/**
 * @brief  FatFs 磁盘读取
 */
DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    sd_err_t err;

    if (!s_sd_initialized) {
        return RES_NOTRDY;
    }

    if (count == 1) {
        err = SD_ReadBlock((uint32_t)sector, buff);
    } else {
        err = SD_ReadBlocks((uint32_t)sector, buff, count);
    }

    return (err == SD_OK) ? RES_OK : RES_ERROR;
}

/**
 * @brief  FatFs 磁盘写入
 */
DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    sd_err_t err;

    if (!s_sd_initialized) {
        return RES_NOTRDY;
    }

    if (count == 1) {
        err = SD_WriteBlock((uint32_t)sector, buff);
    } else {
        err = SD_WriteBlocks((uint32_t)sector, buff, count);
    }

    return (err == SD_OK) ? RES_OK : RES_ERROR;
}

/**
 * @brief  FatFs 磁盘 IOCTL
 */
DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;

    if (!s_sd_initialized) {
        return RES_NOTRDY;
    }

    switch (cmd) {
        case CTRL_SYNC:
            /* 确保写入完成 */
            sd_select();
            sd_wait_ready(SD_TIMEOUT_MS);
            sd_deselect();
            return RES_OK;

        case GET_SECTOR_COUNT:
            if (buff) {
                *(LBA_t *)buff = s_sd_info.block_count;
                return RES_OK;
            }
            break;

        case GET_SECTOR_SIZE:
            if (buff) {
                *(UINT *)buff = SD_BLOCK_SIZE;
                return RES_OK;
            }
            break;

        case GET_BLOCK_SIZE:
            if (buff) {
                *(UINT *)buff = 1;  /* 擦除块大小 = 1 sector (SPI 模式无擦除) */
                return RES_OK;
            }
            break;

        case CTRL_TRIM:
            /* SPI 模式不支持 TRIM */
            return RES_OK;

        default:
            break;
    }

    return RES_PARERR;
}
