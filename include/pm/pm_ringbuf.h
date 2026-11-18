/*
 * pm_ringbuf.h - fixed-capacity byte ring, single producer / single consumer.
 *
 * Contract:
 *  - Capacity must be a power of two; initialized once, never reallocated.
 *  - The producer only advances `head`, the consumer only advances `tail`.
 *    Both indices are 16-bit aligned, so a read is atomic on Cortex-M.
 *  - No critical section is required for a correct SPSC configuration.
 *  - One byte is always kept free to distinguish full from empty, therefore
 *    usable capacity is size-1. Sizes are reported by pm_rb_free().
 *  - Writes never block and never partially fail: the return value tells how
 *    many bytes were stored; the rest must be retried by the caller.
 */
#ifndef PM_RINGBUF_H
#define PM_RINGBUF_H

#include "pm_common.h"
#include "pm_err.h"

typedef struct {
    uint8_t *buf;
    uint16_t mask;                 /* size - 1, size is a power of two */
    volatile uint16_t head;        /* written by the producer only */
    volatile uint16_t tail;        /* written by the consumer only */
    volatile uint32_t dropped;     /* bytes rejected because the ring was full */
} pm_ringbuf_t;

pm_err_t pm_rb_init(pm_ringbuf_t *rb, uint8_t *storage, uint16_t size);

/* Returns the number of bytes actually stored (0..len). */
uint16_t pm_rb_write(pm_ringbuf_t *rb, const uint8_t *src, uint16_t len);

/* Returns the number of bytes actually copied out. */
uint16_t pm_rb_read(pm_ringbuf_t *rb, uint8_t *dst, uint16_t len);

/* Consumer-side advance without copying (used when a DMA/interrupt path has
 * already moved the bytes out). Clamps to the number of available bytes. */
void pm_rb_consume(pm_ringbuf_t *rb, uint16_t len);

uint16_t pm_rb_used(const pm_ringbuf_t *rb);
uint16_t pm_rb_free(const pm_ringbuf_t *rb);
uint32_t pm_rb_dropped(const pm_ringbuf_t *rb);

/* Producer-side reset; must not race with the consumer. */
void pm_rb_reset(pm_ringbuf_t *rb);

#endif /* PM_RINGBUF_H */
