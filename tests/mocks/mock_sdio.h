/**
 * @file mock_sdio.h
 * @brief SDIO 模拟驱动（测试用）
 * @version 3.2
 * @date 2026-05-15
 */

#ifndef MOCK_SDIO_H
#define MOCK_SDIO_H

#include <stdint.h>

void mock_sdio_reset(void);
void mock_sdio_set_rx_data(const uint8_t *data, uint16_t len);
int mock_sdio_send(const uint8_t *data, uint16_t len);
int mock_sdio_recv(uint8_t *data, uint16_t len);

/* 平台接口桩 */
int platform_sdio_send(const uint8_t *data, uint16_t len);
int platform_sdio_recv(uint8_t *data, uint16_t len);

#endif /* MOCK_SDIO_H */
