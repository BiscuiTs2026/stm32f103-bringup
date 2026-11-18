#include "pm_ringbuf.h"

static bool is_power_of_two(uint16_t v)
{
    return (v != 0u) && ((v & (uint16_t)(v - 1u)) == 0u);
}

pm_err_t pm_rb_init(pm_ringbuf_t *rb, uint8_t *storage, uint16_t size)
{
    if ((rb == NULL) || (storage == NULL)) {
        return PM_ERR_NULL;
    }
    if (!is_power_of_two(size) || (size < 2u)) {
        return PM_ERR_PARAM;
    }

    rb->buf     = storage;
    rb->mask    = (uint16_t)(size - 1u);
    rb->head    = 0u;
    rb->tail    = 0u;
    rb->dropped = 0u;
    return PM_OK;
}

uint16_t pm_rb_write(pm_ringbuf_t *rb, const uint8_t *src, uint16_t len)
{
    uint16_t written = 0u;
    uint16_t head;
    uint16_t tail;

    if ((rb == NULL) || (src == NULL)) {
        return 0u;
    }

    head = rb->head;
    tail = rb->tail;

    while (written < len) {
        uint16_t next = (uint16_t)((head + 1u) & rb->mask);
        if (next == tail) {
            break;                    /* full: keep one slot free */
        }
        rb->buf[head] = src[written];
        head = next;
        written++;
    }

    PM_BARRIER();
    rb->head = head;
    rb->dropped += (uint32_t)(len - written);
    return written;
}

uint16_t pm_rb_read(pm_ringbuf_t *rb, uint8_t *dst, uint16_t len)
{
    uint16_t read = 0u;
    uint16_t head;
    uint16_t tail;

    if ((rb == NULL) || (dst == NULL)) {
        return 0u;
    }

    head = rb->head;
    tail = rb->tail;

    while ((read < len) && (tail != head)) {
        dst[read] = rb->buf[tail];
        tail = (uint16_t)((tail + 1u) & rb->mask);
        read++;
    }

    PM_BARRIER();
    rb->tail = tail;
    return read;
}

void pm_rb_consume(pm_ringbuf_t *rb, uint16_t len)
{
    uint16_t used;

    if (rb == NULL) {
        return;
    }

    used = pm_rb_used(rb);
    if (len > used) {
        len = used;
    }
    PM_BARRIER();
    rb->tail = (uint16_t)((rb->tail + len) & rb->mask);
}

uint16_t pm_rb_used(const pm_ringbuf_t *rb)
{
    uint16_t head;
    uint16_t tail;

    if (rb == NULL) {
        return 0u;
    }

    head = rb->head;
    tail = rb->tail;
    return (uint16_t)((head - tail) & rb->mask);
}

uint16_t pm_rb_free(const pm_ringbuf_t *rb)
{
    if (rb == NULL) {
        return 0u;
    }
    /* One slot is reserved to distinguish full from empty. */
    return (uint16_t)(rb->mask - pm_rb_used(rb));
}

uint32_t pm_rb_dropped(const pm_ringbuf_t *rb)
{
    return (rb == NULL) ? 0u : rb->dropped;
}

void pm_rb_reset(pm_ringbuf_t *rb)
{
    if (rb == NULL) {
        return;
    }
    rb->head = 0u;
    rb->tail = 0u;
}
