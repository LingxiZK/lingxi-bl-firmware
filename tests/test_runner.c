/**
 * @file test_runner.c
 * @brief 单元测试主入口
 * @version 3.2
 * @date 2026-05-15
 */

#include "unity.h"

/* 平台接口桩实现（供 debug_log.c 使用） */
uint32_t log_platform_timestamp(void)
{
    static uint32_t ts = 0;
    return ts++;
}

void log_platform_output(const char *str)
{
    printf("%s", str);
}

uint32_t log_platform_thread_id(void)
{
    return 0;
}

/* 外部测试组声明 */
void test_protocol_group(void);
void test_ring_buffer_group(void);
void test_crc16_group(void);
void test_byteorder_group(void);
void test_debug_log_group(void);

int main(void)
{
    UNITY_BEGIN();

    test_protocol_group();
    test_ring_buffer_group();
    test_crc16_group();
    test_byteorder_group();
    test_debug_log_group();

    return UNITY_END();
}
