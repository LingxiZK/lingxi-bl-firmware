/**
 * @file test_debug_log.c
 * @brief 调试日志单元测试
 * @version 3.2
 * @date 2026-05-15
 */

#include "unity.h"
#include "debug_log.h"

void test_log_level_set_get(void)
{
    log_set_level(LOG_LEVEL_DEBUG);
    TEST_ASSERT_EQUAL(LOG_LEVEL_DEBUG, log_get_level());

    log_set_level(LOG_LEVEL_ERROR);
    TEST_ASSERT_EQUAL(LOG_LEVEL_ERROR, log_get_level());
}

void test_log_level_boundary(void)
{
    log_set_level(LOG_LEVEL_MAX);
}

void test_debug_log_group(void)
{
    RUN_TEST(test_log_level_set_get);
    RUN_TEST(test_log_level_boundary);
}
