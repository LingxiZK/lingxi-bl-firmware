/**
 * @file byteorder.h
 * @brief 字节序转换工具
 * @version 3.2
 * @date 2026-05-15
 *
 * STM32N6 和 ESP32-C6 都是小端模式，但协议帧使用大端序。
 * 本模块提供统一转换接口。
 */

#ifndef BYTEORDER_H
#define BYTEORDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 主机字节序检测
 * ============================================================================ */

/** 编译期检测主机字节序 */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #define HOST_LITTLE_ENDIAN 1
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    #define HOST_BIG_ENDIAN 1
#elif defined(__LITTLE_ENDIAN__)
    #define HOST_LITTLE_ENDIAN 1
#elif defined(__BIG_ENDIAN__)
    #define HOST_BIG_ENDIAN 1
#else
    /* 默认假设小端 */
    #define HOST_LITTLE_ENDIAN 1
#endif

/* ============================================================================
 * 16位转换
 * ============================================================================ */

/**
 * @brief 大端序 → 主机序 (16位)
 */
static inline uint16_t be16toh(uint16_t be16)
{
#if HOST_LITTLE_ENDIAN
    return ((be16 >> 8) & 0xFF) | ((be16 << 8) & 0xFF00);
#else
    return be16;
#endif
}

/**
 * @brief 主机序 → 大端序 (16位)
 */
static inline uint16_t htobe16(uint16_t h16)
{
    return be16toh(h16); /* 对称操作 */
}

/**
 * @brief 小端序 → 主机序 (16位)
 */
static inline uint16_t le16toh(uint16_t le16)
{
#if HOST_LITTLE_ENDIAN
    return le16;
#else
    return ((le16 >> 8) & 0xFF) | ((le16 << 8) & 0xFF00);
#endif
}

/**
 * @brief 主机序 → 小端序 (16位)
 */
static inline uint16_t htole16(uint16_t h16)
{
    return le16toh(h16);
}

/* ============================================================================
 * 32位转换
 * ============================================================================ */

/**
 * @brief 大端序 → 主机序 (32位)
 */
static inline uint32_t be32toh(uint32_t be32)
{
#if HOST_LITTLE_ENDIAN
    return ((be32 >> 24) & 0xFF)       |
           ((be32 >> 8)  & 0xFF00)     |
           ((be32 << 8)  & 0xFF0000)   |
           ((be32 << 24) & 0xFF000000);
#else
    return be32;
#endif
}

/**
 * @brief 主机序 → 大端序 (32位)
 */
static inline uint32_t htobe32(uint32_t h32)
{
    return be32toh(h32);
}

/**
 * @brief 小端序 → 主机序 (32位)
 */
static inline uint32_t le32toh(uint32_t le32)
{
#if HOST_LITTLE_ENDIAN
    return le32;
#else
    return ((le32 >> 24) & 0xFF)       |
           ((le32 >> 8)  & 0xFF00)     |
           ((le32 << 8)  & 0xFF0000)   |
           ((le32 << 24) & 0xFF000000);
#endif
}

/**
 * @brief 主机序 → 小端序 (32位)
 */
static inline uint32_t htole32(uint32_t h32)
{
    return le32toh(h32);
}

/* ============================================================================
 * 64位转换
 * ============================================================================ */

/**
 * @brief 大端序 → 主机序 (64位)
 */
static inline uint64_t be64toh(uint64_t be64)
{
#if HOST_LITTLE_ENDIAN
    return ((be64 >> 56) & 0xFFULL)          |
           ((be64 >> 40) & 0xFF00ULL)        |
           ((be64 >> 24) & 0xFF0000ULL)      |
           ((be64 >> 8)  & 0xFF000000ULL)    |
           ((be64 << 8)  & 0xFF00000000ULL)  |
           ((be64 << 24) & 0xFF0000000000ULL)|
           ((be64 << 40) & 0xFF000000000000ULL)|
           ((be64 << 56) & 0xFF00000000000000ULL);
#else
    return be64;
#endif
}

/**
 * @brief 主机序 → 大端序 (64位)
 */
static inline uint64_t htobe64(uint64_t h64)
{
    return be64toh(h64);
}

/* ============================================================================
 * 内存操作（处理未对齐地址）
 * ============================================================================ */

/**
 * @brief 从内存读取大端16位（支持未对齐）
 */
static inline uint16_t be16_read(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

/**
 * @brief 向内存写入大端16位（支持未对齐）
 */
static inline void be16_write(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/**
 * @brief 从内存读取大端32位（支持未对齐）
 */
static inline uint32_t be32_read(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           p[3];
}

/**
 * @brief 向内存写入大端32位（支持未对齐）
 */
static inline void be32_write(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFF);
}

/**
 * @brief 从内存读取小端16位（支持未对齐）
 */
static inline uint16_t le16_read(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}

/**
 * @brief 向内存写入小端16位（支持未对齐）
 */
static inline void le16_write(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

/**
 * @brief 从内存读取小端32位（支持未对齐）
 */
static inline uint32_t le32_read(const uint8_t *p)
{
    return p[0] |
           ((uint32_t)p[1] << 8)  |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/**
 * @brief 向内存写入小端32位（支持未对齐）
 */
static inline void le32_write(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#ifdef __cplusplus
}
#endif

#endif /* BYTEORDER_H */
