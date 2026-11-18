/*
 * pm_event.h - the ISR-to-main-loop notification model.
 *
 * Contract:
 *  - An ISR may only push an event, move bytes, and clear the interrupt flag.
 *  - The event queue is a fixed-size, single-producer / single-consumer ring.
 *    Producers: ISRs. Consumer: the main loop.
 *  - Level-style events (RX_DATA, TX_DONE, ERROR, SPI_DONE) are de-duplicated:
 *    pushing the same (id, src) twice keeps a single entry.
 *  - A full queue drops the newest event and increments `dropped`, which is
 *    observable through pm_evtq_dropped(). Silence is not allowed.
 *  - User callbacks are never executed in ISR context (see pm_uart_poll).
 */
#ifndef PM_EVENT_H
#define PM_EVENT_H

#include "pm_common.h"

#define PM_EVTQ_DEPTH 8u

typedef enum {
    PM_EVT_NONE = 0,
    PM_EVT_UART_RX_DATA = 1,   /* bytes are available in the RX ring */
    PM_EVT_UART_TX_DONE = 2,   /* the TX ring became empty */
    PM_EVT_UART_ERROR   = 3,   /* hardware error; see pm_uart_stats_t */
    PM_EVT_SPI_DONE     = 4,   /* transfer finished; arg = bytes clocked */
    PM_EVT_SPI_ERROR    = 5    /* hardware error; arg = backend error mask */
} pm_evt_id_t;

typedef struct {
    pm_evt_id_t id;
    uint8_t     src;   /* logical instance id (hw_id) */
    uint16_t    arg;   /* event-specific payload, 0 if unused */
} pm_evt_t;

typedef struct {
    pm_evt_t item[PM_EVTQ_DEPTH];
    volatile uint8_t  head;     /* producer index (ISR) */
    volatile uint8_t  tail;     /* consumer index (main loop) */
    volatile uint32_t dropped;
} pm_evtq_t;

void        pm_evtq_init(pm_evtq_t *q);
void        pm_evtq_push(pm_evtq_t *q, pm_evt_id_t id, uint8_t src, uint16_t arg);
bool        pm_evtq_pop(pm_evtq_t *q, pm_evt_t *out);
uint32_t    pm_evtq_dropped(const pm_evtq_t *q);

#endif /* PM_EVENT_H */
