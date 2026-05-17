/*******************************************************************************
 * @file    ring_buffer.c
 ******************************************************************************/
#include "ring_buffer.h"

void rb_init(ring_buffer_t *rb, uint8_t *buf, uint16_t size)
{
    rb->buf = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

bool rb_put(ring_buffer_t *rb, uint8_t data)
{
    if (rb_is_full(rb)) return false;
    rb->buf[rb->head] = data;
    rb->head = (rb->head + 1) % rb->size;
    rb->count++;
    return true;
}

bool rb_get(ring_buffer_t *rb, uint8_t *data)
{
    if (rb_is_empty(rb)) return false;
    *data = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    rb->count--;
    return true;
}

uint16_t rb_available(ring_buffer_t *rb)
{
    return rb->count;
}

bool rb_is_empty(ring_buffer_t *rb)
{
    return rb->count == 0;
}

bool rb_is_full(ring_buffer_t *rb)
{
    return rb->count >= rb->size;
}

void rb_clear(ring_buffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}
