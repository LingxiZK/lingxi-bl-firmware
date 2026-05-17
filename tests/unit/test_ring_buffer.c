/**
 * @file test_ring_buffer.c
 * @brief 环形缓冲区单元测试
 * @version 3.2
 * @date 2026-05-15
 */

#include "unity.h"
#include "ring_buffer.h"

static uint8_t buf_mem[256];
static ring_buffer_t rb;

void setUp(void)
{
    rb_init(&rb, buf_mem, sizeof(buf_mem));
}

void tearDown(void)
{
}

void test_init(void)
{
    TEST_ASSERT_TRUE(rb_is_empty(&rb));
    TEST_ASSERT_EQUAL(255, rb_free(&rb));
    TEST_ASSERT_EQUAL(0, rb_used(&rb));
}

void test_write_read(void)
{
    uint8_t data[] = {1, 2, 3, 4, 5};
    uint8_t out[5];

    TEST_ASSERT_EQUAL(5, rb_write(&rb, data, 5));
    TEST_ASSERT_EQUAL(5, rb_used(&rb));

    TEST_ASSERT_EQUAL(5, rb_read(&rb, out, 5));
    TEST_ASSERT_TRUE(rb_is_empty(&rb));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, 5);
}

void test_wraparound(void)
{
    uint8_t data[250];
    uint8_t out[250];

    memset(data, 0xAA, sizeof(data));
    rb_write(&rb, data, sizeof(data));

    rb_read(&rb, out, 125);

    memset(data, 0xBB, sizeof(data));
    rb_write(&rb, data, 125);

    rb_read(&rb, out, 125);
    for (int i = 0; i < 125; i++)
        TEST_ASSERT_EQUAL(0xAA, out[i]);

    rb_read(&rb, out, 125);
    for (int i = 0; i < 125; i++)
        TEST_ASSERT_EQUAL(0xBB, out[i]);
}

void test_overflow(void)
{
    uint8_t data[300];

    memset(data, 0xCC, sizeof(data));
    uint16_t written = rb_write(&rb, data, sizeof(data));

    TEST_ASSERT_EQUAL(255, written);
    TEST_ASSERT_EQUAL(1, rb.overflow_cnt);
}

void test_peek(void)
{
    uint8_t data[] = {0x11, 0x22, 0x33};
    uint8_t out1[3], out2[3];

    rb_write(&rb, data, 3);

    TEST_ASSERT_EQUAL(3, rb_peek(&rb, out1, 3));
    TEST_ASSERT_EQUAL(3, rb_peek(&rb, out2, 3));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out1, 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out2, 3);
    TEST_ASSERT_EQUAL(3, rb_used(&rb));
}

void test_all_or_nothing(void)
{
    uint8_t data[300];

    TEST_ASSERT_EQUAL(0, rb_write_all_or_nothing(&rb, data, sizeof(data)));
    TEST_ASSERT_TRUE(rb_is_empty(&rb));
}

void test_drop(void)
{
    uint8_t data[] = {1, 2, 3, 4, 5};
    uint8_t out[3];

    rb_write(&rb, data, 5);
    TEST_ASSERT_EQUAL(2, rb_drop(&rb, 2));
    TEST_ASSERT_EQUAL(3, rb_read(&rb, out, 3));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&data[2], out, 3);
}

void test_ring_buffer_group(void)
{
    RUN_TEST(test_init);
    RUN_TEST(test_write_read);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_overflow);
    RUN_TEST(test_peek);
    RUN_TEST(test_all_or_nothing);
    RUN_TEST(test_drop);
}
