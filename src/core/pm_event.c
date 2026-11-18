#include "pm_event.h"

void pm_evtq_init(pm_evtq_t *q)
{
    uint8_t i;

    if (q == NULL) {
        return;
    }
    for (i = 0u; i < PM_EVTQ_DEPTH; i++) {
        q->item[i].id  = PM_EVT_NONE;
        q->item[i].src = 0u;
        q->item[i].arg = 0u;
    }
    q->head    = 0u;
    q->tail    = 0u;
    q->dropped = 0u;
}

void pm_evtq_push(pm_evtq_t *q, pm_evt_id_t id, uint8_t src, uint16_t arg)
{
    uint8_t head;
    uint8_t tail;
    uint8_t i;

    if ((q == NULL) || (id == PM_EVT_NONE)) {
        return;
    }

    head = q->head;
    tail = q->tail;

    /* Level-style events are de-duplicated so a burst of RX interrupts does
     * not fill the queue with identical entries. */
    i = tail;
    while (i != head) {
        if ((q->item[i].id == id) && (q->item[i].src == src)) {
            q->item[i].arg = arg;
            return;
        }
        i = (uint8_t)((i + 1u) % PM_EVTQ_DEPTH);
    }

    {
        uint8_t next = (uint8_t)((head + 1u) % PM_EVTQ_DEPTH);
        if (next == tail) {
            q->dropped++;
            return;                    /* full: drop newest, stay observable */
        }
        q->item[head].id  = id;
        q->item[head].src = src;
        q->item[head].arg = arg;
        PM_BARRIER();
        q->head = next;
    }
}

bool pm_evtq_pop(pm_evtq_t *q, pm_evt_t *out)
{
    uint8_t head;
    uint8_t tail;

    if ((q == NULL) || (out == NULL)) {
        return false;
    }

    head = q->head;
    tail = q->tail;
    if (head == tail) {
        return false;
    }

    *out = q->item[tail];
    PM_BARRIER();
    q->tail = (uint8_t)((tail + 1u) % PM_EVTQ_DEPTH);
    return true;
}

uint32_t pm_evtq_dropped(const pm_evtq_t *q)
{
    return (q == NULL) ? 0u : q->dropped;
}
