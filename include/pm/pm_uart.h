/*
 * pm_uart.h - asynchronous UART model API.
 *
 * Lifecycle:  init -> (read/write/poll)* -> deinit
 *
 * Ownership and context rules (see docs/00-contract.md, section 5):
 *  - rx_storage / tx_storage are owned by the caller and must stay alive
 *    for the whole lifetime of the object. The model never copies them.
 *  - pm_uart_write() queues bytes and returns; the port moves them out of
 *    the ring in TX-complete interrupts. It never busy-waits on TXE.
 *  - Received bytes are moved into the RX ring by the ISR only.
 *  - pm_uart_poll() is the only place where an application callback runs,
 *    always in main-loop context. ISRs never call user code.
 *  - All functions except the pm_uart_hw_* internal hooks may be called from
 *    the main loop only.
 */
#ifndef PM_UART_H
#define PM_UART_H

#include "pm_common.h"
#include "pm_err.h"
#include "pm_port.h"
#include "pm_event.h"
#include "pm_ringbuf.h"

#define PM_UART_TIMEOUT_NOW 0u   /* non-blocking */

struct pm_uart {
    uint32_t magic;         /* set by pm_uart_init, cleared by pm_uart_deinit */
    uint8_t  state;
    uint8_t  hw_id;
    uint16_t tx_inflight;   /* bytes handed to the port, not yet completed */

    pm_ringbuf_t rx;
    pm_ringbuf_t tx;
    pm_evtq_t    evtq;

    const pm_uart_ops_t *ops;
    void                *port_ctx;
    pm_uart_stats_t      stats;
};

typedef void (*pm_uart_evt_cb_t)(pm_uart_t *u, const pm_evt_t *evt, void *user_ctx);

/* Initializes the object and opens the hardware instance described by cfg.
 * The object must be zero-initialized (static storage or `= {0}`).
 * The port is selected through pm_port_get(); cfg->hw_id selects the
 * instance. Returns PM_ERR_RANGE for an unknown hw_id, PM_ERR_BUSY when the
 * object or the hardware instance is already open. */
pm_err_t pm_uart_init(pm_uart_t *u, const pm_uart_cfg_t *cfg);

/* Disables interrupts for this instance and releases the port context.
 * Safe to call on a partially initialized object. */
pm_err_t pm_uart_deinit(pm_uart_t *u);

/* Queues `len` bytes.
 *  timeout == 0            : non-blocking; returns PM_ERR_AGAIN if the ring
 *                            could not take everything.
 *  timeout > 0             : waits up to timeout ms for ring space.
 *  On PM_ERR_TIMEOUT/AGAIN, *written reports how much was accepted. */
pm_err_t pm_uart_write(pm_uart_t *u, const uint8_t *data, size_t len,
                       size_t *written, uint32_t timeout_ms);

/* Copies up to `len` bytes from the RX ring. Never blocks.
 * Returns PM_OK (possibly with *read == 0) or PM_ERR_AGAIN when empty. */
pm_err_t pm_uart_read(pm_uart_t *u, uint8_t *data, size_t len, size_t *read);

/* Pops one pending event into *evt (may be NULL).
 * If `cb` is non-NULL it is invoked with the popped event before returning,
 * in main-loop context. Returns false when the queue is empty. */
bool pm_uart_poll(pm_uart_t *u, pm_evt_t *evt, pm_uart_evt_cb_t cb, void *user_ctx);

/* Discards received bytes that have not been read yet. */
pm_err_t pm_uart_flush_rx(pm_uart_t *u);

pm_err_t pm_uart_get_stats(const pm_uart_t *u, pm_uart_stats_t *out);

/* -------------------------------------------------- ISR-side internal hooks
 * These are called only by the port backend, from interrupt context.
 * They never call user code and never block. */
void pm_uart_hw_rx_byte(pm_uart_t *u, uint8_t byte);
void pm_uart_hw_tx_complete(pm_uart_t *u);
void pm_uart_hw_error(pm_uart_t *u, uint16_t hw_error_mask);

#endif /* PM_UART_H */
