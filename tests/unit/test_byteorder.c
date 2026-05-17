/**
 * @file test_byteorder.c
 * @brief 字节序转换单元测试
 * @version 3.2
 * @date 2026-05-15
 */

#include "unity.h"
#include "byteorder.h"

void test_be16(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x3412, be16toh(0x1234));
    TEST_ASSERT_EQUAL_HEX16(0x1234, htobe16(0x1234));
}

void test_be32(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x78563412, be32toh(0x12345678));
}

void test_le16(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x1234, le16toh(0x1234));
}

void test_memory_read_write(void)
{
    uint8_t buf[4];

    be32_write(buf, 0x12345678);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x78, buf[3]);

    TEST_ASSERT_EQUAL_HEX32(0x12345678, be32_read(buf));
}

void test_be16_memory(void)
{
    uint8_t buf[2];
    be16_write(buf, 0xABCD);
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[1]);
    TEST_ASSERT_EQUAL_HEX16(0xABCD, be16_read(buf));
}

void test_byteorder_group(void)
{
    RUN_TEST(test_be16);
    RUN_TEST(test_be32);
    RUN_TEST(test_le16);
    RUN_TEST(test_memory_read_write);
    RUN_TEST(test_be16_memory);
}
