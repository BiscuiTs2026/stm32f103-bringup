#include "pm_uart.h"
#include "pm_osal.h"
#include <string.h>

#define PM_UART_STATE_UNINIT 0u
#define PM_UART_STATE_READY  1u

#define PM_UART_MAGIC        0x50554D31u   /* "PUM1" */

#define PM_UART_MIN_RX_SIZE  32u
#define PM_UART_MIN_TX_SIZE  64u
#define PM_UART_BAUD_MIN     1200u
#define PM_UART_BAUD_MAX     4000000u

static bool is_power_of_two(uint16_t v)
{
    return (v != 0u) && ((v & (uint16_t)(v - 1u)) == 0u);
}

/* Must be called with interrupts disabled (main loop) or from the TX ISR. */
static void uart_kick_tx_locked(pm_uart_t *u)
{
    uint16_t used;
    uint16_t tail;
    uint16_t contig;

    if (u->tx_inflight != 0u) {
        return;                       /* previous chunk still on the wire */
    }

    used = pm_rb_used(&u->tx);
    if (used == 0u) {
        return;
    }

    tail = u->tx.tail;
    contig = (uint16_t)((u->tx.mask + 1u) - tail);
    if (contig > used) {
        contig = used;
    }

    u->tx_inflight = contig;
    if (u->ops->start_tx(u->port_ctx, u, &u->tx.buf[tail], contig) != PM_OK) {
        u->tx_inflight = 0u;
        u->stats.hw_errors++;
        pm_evtq_push(&u->evtq, PM_EVT_UART_ERROR, u->hw_id, 0u);
    }
}

static pm_err_t validate_cfg(const pm_uart_t *u, const pm_uart_cfg_t *cfg)
{
    if ((u == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    if ((cfg->rx_storage == NULL) || (cfg->tx_storage == NULL)) {
        return PM_ERR_NULL;
    }
    if ((cfg->data_bits != 8u) ||
        (cfg->baudrate < PM_UART_BAUD_MIN) ||
        (cfg->baudrate > PM_UART_BAUD_MAX) ||
        ((cfg->parity != PM_UART_PARITY_NONE) &&
         (cfg->parity != PM_UART_PARITY_EVEN) &&
         (cfg->parity != PM_UART_PARITY_ODD)) ||
        ((cfg->stop_bits != PM_UART_STOP_1) && (cfg->stop_bits != PM_UART_STOP_2))) {
        return PM_ERR_PARAM;
    }
    if (!is_power_of_two(cfg->rx_size) || (cfg->rx_size < PM_UART_MIN_RX_SIZE) ||
        !is_power_of_two(cfg->tx_size) || (cfg->tx_size < PM_UART_MIN_TX_SIZE)) {
        return PM_ERR_PARAM;
    }
    return PM_OK;
}

pm_err_t pm_uart_init(pm_uart_t *u, const pm_uart_cfg_t *cfg)
{
    const pm_port_t *port;
    pm_err_t rc;

    rc = validate_cfg(u, cfg);
    if (rc != PM_OK) {
        return rc;
    }
    if ((u->magic == PM_UART_MAGIC) && (u->state == PM_UART_STATE_READY)) {
        return PM_ERR_BUSY;           /* object already owns its instance */
    }

    port = pm_port_get();
    if ((port == NULL) || (port->uart == NULL)) {
        return PM_ERR_STATE;
    }
    if (cfg->hw_id >= port->uart_count) {
        return PM_ERR_RANGE;
    }

    (void)memset(u, 0, sizeof(*u));
    u->state    = PM_UART_STATE_UNINIT;
    u->hw_id    = cfg->hw_id;
    u->ops      = port->uart;
    u->port_ctx = port->uart_ctx;

    rc = pm_rb_init(&u->rx, cfg->rx_storage, cfg->rx_size);
    if (rc != PM_OK) {
        return rc;
    }
    rc = pm_rb_init(&u->tx, cfg->tx_storage, cfg->tx_size);
    if (rc != PM_OK) {
        return rc;
    }
    pm_evtq_init(&u->evtq);

    rc = u->ops->open(u->port_ctx, u, cfg);
    if (rc != PM_OK) {
        u->ops = NULL;
        return rc;
    }

    u->state = PM_UART_STATE_READY;
    u->magic = PM_UART_MAGIC;
    u->ops->enable_rx(u->port_ctx, u);
    return PM_OK;
}

pm_err_t pm_uart_deinit(pm_uart_t *u)
{
    if (u == NULL) {
        return PM_ERR_NULL;
    }
    if (u->state != PM_UART_STATE_READY) {
        return PM_OK;                 /* already closed */
    }

    u->ops->disable_rx(u->port_ctx, u);
    u->ops->close(u->port_ctx, u);
    u->state = PM_UART_STATE_UNINIT;
    u->magic = 0u;
    u->ops   = NULL;
    return PM_OK;
}

pm_err_t pm_uart_write(pm_uart_t *u, const uint8_t *data, size_t len,
                       size_t *written, uint32_t timeout_ms)
{
    size_t total = 0u;
    uint32_t start;

    if (written != NULL) {
        *written = 0u;
    }
    if ((u == NULL) || (data == NULL)) {
        return PM_ERR_NULL;
    }
    if (u->state != PM_UART_STATE_READY) {
        return PM_ERR_STATE;
    }
    if (len == 0u) {
        return PM_OK;
    }

    start = pm_osal_now_ms();

    for (;;) {
        pm_irq_state_t irq;
        uint16_t chunk;
        uint16_t accepted;

        chunk = (uint16_t)(((len - total) > 0xFFFFu) ? 0xFFFFu : (len - total));

        irq = pm_osal_critical_enter();
        accepted = pm_rb_write(&u->tx, &data[total], chunk);
        uart_kick_tx_locked(u);
        pm_osal_critical_exit(irq);

        total += accepted;

        if (total == len) {
            if (written != NULL) {
                *written = total;
            }
            return PM_OK;
        }

        if (timeout_ms == 0u) {
            u->stats.tx_dropped += (uint32_t)(len - total);
            if (written != NULL) {
                *written = total;
            }
            return PM_ERR_AGAIN;
        }

        if ((uint32_t)(pm_osal_now_ms() - start) >= timeout_ms) {
            u->stats.tx_dropped += (uint32_t)(len - total);
            if (written != NULL) {
                *written = total;
            }
            return PM_ERR_TIMEOUT;
        }

        pm_osal_delay_ms(1u);
    }
}

pm_err_t pm_uart_read(pm_uart_t *u, uint8_t *data, size_t len, size_t *read)
{
    uint16_t got;

    if (read != NULL) {
        *read = 0u;
    }
    if ((u == NULL) || (data == NULL)) {
        return PM_ERR_NULL;
    }
    if (u->state != PM_UART_STATE_READY) {
        return PM_ERR_STATE;
    }
    if (len == 0u) {
        return PM_OK;
    }

    {
        uint16_t chunk = (uint16_t)((len > 0xFFFFu) ? 0xFFFFu : len);
        got = pm_rb_read(&u->rx, data, chunk);
    }

    if (read != NULL) {
        *read = got;
    }
    return (got == 0u) ? PM_ERR_AGAIN : PM_OK;
}

bool pm_uart_poll(pm_uart_t *u, pm_evt_t *evt, pm_uart_evt_cb_t cb, void *user_ctx)
{
    pm_evt_t local;

    if (u == NULL) {
        return false;
    }
    if (!pm_evtq_pop(&u->evtq, &local)) {
        return false;
    }
    if (evt != NULL) {
        *evt = local;
    }
    if (cb != NULL) {
        cb(u, &local, user_ctx);      /* main-loop context by contract */
    }
    return true;
}

pm_err_t pm_uart_flush_rx(pm_uart_t *u)
{
    pm_irq_state_t irq;

    if (u == NULL) {
        return PM_ERR_NULL;
    }
    if (u->state != PM_UART_STATE_READY) {
        return PM_ERR_STATE;
    }

    irq = pm_osal_critical_enter();
    pm_rb_reset(&u->rx);
    pm_osal_critical_exit(irq);
    return PM_OK;
}

pm_err_t pm_uart_get_stats(const pm_uart_t *u, pm_uart_stats_t *out)
{
    if ((u == NULL) || (out == NULL)) {
        return PM_ERR_NULL;
    }
    *out = u->stats;
    return PM_OK;
}

/* ------------------------------------------------------------ ISR hooks */

void pm_uart_hw_rx_byte(pm_uart_t *u, uint8_t byte)
{
    if ((u == NULL) || (u->state != PM_UART_STATE_READY)) {
        return;
    }

    if (pm_rb_write(&u->rx, &byte, 1u) == 1u) {
        u->stats.rx_bytes++;
        pm_evtq_push(&u->evtq, PM_EVT_UART_RX_DATA, u->hw_id, 0u);
    } else {
        u->stats.rx_dropped++;        /* ring already counted the byte */
        pm_evtq_push(&u->evtq, PM_EVT_UART_ERROR, u->hw_id, 0u);
    }
}

void pm_uart_hw_tx_complete(pm_uart_t *u)
{
    uint16_t done;

    if ((u == NULL) || (u->state != PM_UART_STATE_READY)) {
        return;
    }

    done = u->tx_inflight;
    if (done == 0u) {
        return;
    }

    u->tx_inflight = 0u;
    pm_rb_consume(&u->tx, done);
    u->stats.tx_bytes += done;

    if (pm_rb_used(&u->tx) == 0u) {
        pm_evtq_push(&u->evtq, PM_EVT_UART_TX_DONE, u->hw_id, 0u);
    } else {
        uart_kick_tx_locked(u);
    }
}

void pm_uart_hw_error(pm_uart_t *u, uint16_t hw_error_mask)
{
    if ((u == NULL) || (u->state != PM_UART_STATE_READY)) {
        return;
    }

    u->stats.hw_errors++;
    u->stats.last_hw_error = hw_error_mask;
    pm_evtq_push(&u->evtq, PM_EVT_UART_ERROR, u->hw_id, hw_error_mask);
}
