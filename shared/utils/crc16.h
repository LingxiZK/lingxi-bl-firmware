/**
 * @file crc16.h
 * @brief CRC16 校验接口
 * @version 3.2
 * @date 2026-05-15
 *
 * 提供多种CRC16变体，默认使用CCITT-FALSE（与lingxi_protocol.c一致）
 */

#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CRC16 算法变体
 */
typedef enum {
    CRC16_CCITT_FALSE,  /**< CCITT-FALSE: poly=0x1021, init=0xFFFF */
    CRC16_CCITT_TRUE,   /**< CCITT-TRUE:  poly=0x1021, init=0x0000 */
    CRC16_MODBUS,       /**< MODBUS:      poly=0x8005, init=0xFFFF */
    CRC16_IBM,          /**< IBM:         poly=0x8005, init=0x0000 */
    CRC16_MAX
} crc16_variant_t;

/**
 * @brief 计算CRC16
 * @param data      数据指针
 * @param len       数据长度
 * @param variant   CRC16变体
 * @return CRC16值
 */
uint16_t crc16_calculate(const uint8_t *data, uint16_t len, crc16_variant_t variant);

/**
 * @brief 增量计算CRC16（用于流式数据）
 * @param crc       当前CRC值
 * @param byte      新字节
 * @param variant   CRC16变体
 * @return 更新后的CRC值
 */
uint16_t crc16_update(uint16_t crc, uint8_t byte, crc16_variant_t variant);

/**
 * @brief 快速CRC16（CCITT-FALSE，查表法）
 * @param data  数据指针
 * @param len   数据长度
 * @return CRC16值
 */
uint16_t crc16_ccitt_fast(const uint8_t *data, uint16_t len);

/**
 * @brief 验证CRC16
 * @param data      数据指针（不含CRC）
 * @param len       数据长度
 * @param expected  期望的CRC值
 * @param variant   CRC16变体
 * @return true=校验通过
 */
bool crc16_verify(const uint8_t *data, uint16_t len, uint16_t expected, crc16_variant_t variant);

#ifdef __cplusplus
}
#endif

#endif /* CRC16_H */
