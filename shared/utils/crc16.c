/**
 * @file crc16.c
 * @brief CRC16 校验实现
 * @version 3.2
 * @date 2026-05-15
 */

#include "crc16.h"

/* ============================================================================
 * CRC16 查找表（CCITT-FALSE）
 * ============================================================================ */

static const uint16_t crc16_ccitt_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x12D0, 0x21E3, 0x30F2, 0x0691, 0x16B0, 0x6657, 0x7676,
    0x4615, 0x5634, 0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9,
    0xB98A, 0xA9AB, 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1,
    0x3882, 0x28A3, 0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8,
    0xABBB, 0xBB9A, 0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0,
    0x2AB3, 0x3A92, 0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B,
    0x9DE8, 0x8DC9, 0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83,
    0x1CE0, 0x0CC1, 0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA,
    0x8FD9, 0x9FF8, 0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2,
    0x0ED1, 0x1EF0
};

/* ============================================================================
 * 通用计算（逐位法，适用于所有变体）
 * ============================================================================ */

typedef struct {
    uint16_t poly;
    uint16_t init;
    uint16_t xorout;
    bool     refin;
    bool     refout;
} crc16_params_t;

static const crc16_params_t crc16_params[CRC16_MAX] = {
    [CRC16_CCITT_FALSE] = { 0x1021, 0xFFFF, 0x0000, false, false },
    [CRC16_CCITT_TRUE]  = { 0x1021, 0x0000, 0x0000, true,  true  },
    [CRC16_MODBUS]      = { 0x8005, 0xFFFF, 0x0000, true,  true  },
    [CRC16_IBM]         = { 0x8005, 0x0000, 0x0000, true,  true  },
};

static uint16_t crc16_bitwise(const uint8_t *data, uint16_t len, crc16_variant_t variant)
{
    const crc16_params_t *p = &crc16_params[variant];
    uint16_t crc = p->init;

    for (uint16_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        if (p->refin) {
            /* 反转字节 */
            byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
            byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
            byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
        }
        crc ^= ((uint16_t)byte << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ p->poly;
            } else {
                crc <<= 1;
            }
        }
    }

    if (p->refout) {
        /* 反转CRC */
        uint16_t rev = 0;
        for (int i = 0; i < 16; i++) {
            rev |= ((crc >> i) & 1) << (15 - i);
        }
        crc = rev;
    }

    return crc ^ p->xorout;
}

/* ============================================================================
 * 公共API
 * ============================================================================ */

uint16_t crc16_calculate(const uint8_t *data, uint16_t len, crc16_variant_t variant)
{
    if (data == NULL || len == 0 || variant >= CRC16_MAX) {
        return 0;
    }

    if (variant == CRC16_CCITT_FALSE) {
        return crc16_ccitt_fast(data, len);
    }

    return crc16_bitwise(data, len, variant);
}

uint16_t crc16_update(uint16_t crc, uint8_t byte, crc16_variant_t variant)
{
    if (variant >= CRC16_MAX) {
        return 0;
    }

    if (variant == CRC16_CCITT_FALSE) {
        return (crc << 8) ^ crc16_ccitt_table[((crc >> 8) ^ byte) & 0xFF];
    }

    /* 其他变体：逐位更新（效率较低，不建议流式使用） */
    uint8_t tmp[1] = { byte };
    return crc16_bitwise(tmp, 1, variant);
}

uint16_t crc16_ccitt_fast(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc = (crc << 8) ^ crc16_ccitt_table[((crc >> 8) ^ *data++) & 0xFF];
    }
    return crc;
}

bool crc16_verify(const uint8_t *data, uint16_t len, uint16_t expected, crc16_variant_t variant)
{
    uint16_t calc = crc16_calculate(data, len, variant);
    return (calc == expected);
}
