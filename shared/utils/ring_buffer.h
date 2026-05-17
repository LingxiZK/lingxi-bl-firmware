/*******************************************************************************
 * @file    ring_buffer.h
 * @brief   线程安全环形缓冲区
 ******************************************************************************/
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  *buf;
    uint16_t  size;
    uint16_t  head;
    uint16_t  tail;
    uint16_t  count;
} ring_buffer_t;

void rb_init(ring_buffer_t *rb, uint8_t *buf, uint16_t size);
bool rb_put(ring_buffer_t *rb, uint8_t data);
bool rb_get(ring_buffer_t *rb, uint8_t *data);
uint16_t rb_available(ring_buffer_t *rb);
bool rb_is_empty(ring_buffer_t *rb);
bool rb_is_full(ring_buffer_t *rb);
void rb_clear(ring_buffer_t *rb);

#ifdef __cplusplus
}
#endif

#endif
