/**
 * @file test_crc16.c
 * @brief CRC16 单元测试
 * @version 3.2
 * @date 2026-05-15
 */

#include "unity.h"
#include "crc16.h"

void test_crc16_ccitt_zero(void)
{
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
    uint16_t crc = crc16_ccitt_fast(data, sizeof(data));
    TEST_ASSERT_EQUAL_HEX16(0xC2FB, crc);
}

void test_crc16_empty(void)
{
    uint16_t crc = crc16_ccitt_fast(NULL, 0);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc);
}

void test_crc16_modbus(void)
{
    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint16_t crc = crc16_calculate(data, sizeof(data), CRC16_MODBUS);
    TEST_ASSERT_EQUAL_HEX16(0xC40F, crc);
}

void test_crc16_verify(void)
{
    uint8_t data[] = "123456789";
    uint16_t crc = crc16_ccitt_fast(data, sizeof(data) - 1);
    TEST_ASSERT_TRUE(crc16_verify(data, sizeof(data) - 1, crc, CRC16_CCITT_FALSE));
}

void test_crc16_incremental(void)
{
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t crc1 = crc16_ccitt_fast(data, 3);

    uint16_t crc2 = 0xFFFF;
    for (int i = 0; i < 3; i++) {
        crc2 = crc16_update(crc2, data[i], CRC16_CCITT_FALSE);
    }

    TEST_ASSERT_EQUAL_HEX16(crc1, crc2);
}

void test_crc16_group(void)
{
    RUN_TEST(test_crc16_ccitt_zero);
    RUN_TEST(test_crc16_empty);
    RUN_TEST(test_crc16_modbus);
    RUN_TEST(test_crc16_verify);
    RUN_TEST(test_crc16_incremental);
}
