/**
 * @file byteorder.c
 * @brief 字节序转换实现（仅含非内联函数）
 * @version 3.2
 * @date 2026-05-15
 *
 * 大部分函数为static inline，定义在头文件中。
 * 本文件仅包含需要非内联实现的函数（如运行时检测）。
 */

#include "byteorder.h"

/**
 * @brief 运行时检测主机字节序
 * @return 1=大端, 0=小端
 */
int byteorder_is_big_endian(void)
{
    union {
        uint32_t i;
        uint8_t c[4];
    } test = { .i = 0x01020304 };

    return (test.c[0] == 0x01);
}
