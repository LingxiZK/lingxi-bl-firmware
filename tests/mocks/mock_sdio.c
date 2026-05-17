/**
 * @file mock_sdio.c
 * @brief SDIO 模拟驱动实现
 * @version 3.2
 * @date 2026-05-15
 */

#include "mock_sdio.h"
#include <string.h>

static uint8_t mock_tx_buf[4096];
static uint16_t mock_tx_len = 0;
static uint8_t mock_rx_buf[4096];
static uint16_t mock_rx_len = 0;
static uint16_t mock_rx_pos = 0;

void mock_sdio_reset(void)
{
    memset(mock_tx_buf, 0, sizeof(mock_tx_buf));
    mock_tx_len = 0;
    memset(mock_rx_buf, 0, sizeof(mock_rx_buf));
    mock_rx_len = 0;
    mock_rx_pos = 0;
}

void mock_sdio_set_rx_data(const uint8_t *data, uint16_t len)
{
    if (len > sizeof(mock_rx_buf)) len = sizeof(mock_rx_buf);
    memcpy(mock_rx_buf, data, len);
    mock_rx_len = len;
    mock_rx_pos = 0;
}

int mock_sdio_send(const uint8_t *data, uint16_t len)
{
    if (mock_tx_len + len > sizeof(mock_tx_buf)) return -1;
    memcpy(mock_tx_buf + mock_tx_len, data, len);
    mock_tx_len += len;
    return len;
}

int mock_sdio_recv(uint8_t *data, uint16_t len)
{
    uint16_t avail = mock_rx_len - mock_rx_pos;
    if (len > avail) len = avail;
    memcpy(data, mock_rx_buf + mock_rx_pos, len);
    mock_rx_pos += len;
    return len;
}

int platform_sdio_send(const uint8_t *data, uint16_t len)
{
    return mock_sdio_send(data, len);
}

int platform_sdio_recv(uint8_t *data, uint16_t len)
{
    return mock_sdio_recv(data, len);
}
