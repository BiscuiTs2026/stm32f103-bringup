/*
 * test_main.c - host-side tests for the vendor-free core.
 *
 * The same src/core/*.c files that are compiled into the firmware are
 * compiled here against a mock port and a host OSAL. No hardware involved.
 */
#include <stdio.h>
#include <string.h>

#include "pm_err.h"
#include "pm_ringbuf.h"
#include "pm_event.h"
#include "pm_gpio.h"
#include "pm_uart.h"
#include "pm_port_mock.h"

void host_time_advance(uint32_t ms);
void host_time_reset(void);

static int s_failures;
static int s_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        s_checks++;                                                          \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

/* --------------------------------------------------------------- ringbuf */

static void test_ringbuf(void)
{
    uint8_t storage[8];
    pm_ringbuf_t rb;
    uint8_t out[8];
    uint8_t src[16];

    CHECK(pm_rb_init(NULL, storage, 8) == PM_ERR_NULL);
    CHECK(pm_rb_init(&rb, NULL, 8) == PM_ERR_NULL);
    CHECK(pm_rb_init(&rb, storage, 3) == PM_ERR_PARAM);      /* not power of 2 */
    CHECK(pm_rb_init(&rb, storage, 0) == PM_ERR_PARAM);
    CHECK(pm_rb_init(&rb, storage, 8) == PM_OK);

    CHECK(pm_rb_free(&rb) == 7);                             /* one slot reserved */
    memset(src, 0xA5, sizeof(src));
    CHECK(pm_rb_write(&rb, src, 5) == 5);
    CHECK(pm_rb_used(&rb) == 5);
    CHECK(pm_rb_free(&rb) == 2);

    CHECK(pm_rb_write(&rb, src, 5) == 2);                    /* only 2 slots left */
    CHECK(pm_rb_dropped(&rb) == 3);

    CHECK(pm_rb_read(&rb, out, sizeof(out)) == 7);
    CHECK(out[0] == 0xA5);
    CHECK(pm_rb_used(&rb) == 0);
    CHECK(pm_rb_read(&rb, out, sizeof(out)) == 0);

    /* Wrap-around: write/read several times so head passes the end. */
    CHECK(pm_rb_write(&rb, src, 6) == 6);
    CHECK(pm_rb_read(&rb, out, 4) == 4);
    CHECK(pm_rb_write(&rb, src, 5) == 5);
    CHECK(pm_rb_read(&rb, out, 8) == 7);
    CHECK(pm_rb_used(&rb) == 0);

    /* consume() advances the tail without copying. */
    CHECK(pm_rb_write(&rb, src, 7) == 7);
    pm_rb_consume(&rb, 3);
    CHECK(pm_rb_used(&rb) == 4);
    pm_rb_consume(&rb, 100);                                 /* clamps */
    CHECK(pm_rb_used(&rb) == 0);
}

/* ----------------------------------------------------------------- evtq */

static void test_evtq(void)
{
    pm_evtq_t q;
    pm_evt_t e;

    pm_evtq_init(&q);

    pm_evtq_push(&q, PM_EVT_UART_RX_DATA, 1u, 0u);
    pm_evtq_push(&q, PM_EVT_UART_RX_DATA, 1u, 0u);           /* de-duplicated */
    CHECK(pm_evtq_pop(&q, &e) == true);
    CHECK(e.id == PM_EVT_UART_RX_DATA);
    CHECK(pm_evtq_pop(&q, &e) == false);

    pm_evtq_push(&q, PM_EVT_UART_RX_DATA, 1u, 0u);
    pm_evtq_push(&q, PM_EVT_UART_RX_DATA, 2u, 0u);           /* different src */
    CHECK(pm_evtq_pop(&q, &e) == true);
    CHECK(e.src == 1u);
    CHECK(pm_evtq_pop(&q, &e) == true);
    CHECK(e.src == 2u);

    /* Fill beyond depth: the newest is dropped and counted.
     * Capacity is PM_EVTQ_DEPTH-1 because one slot separates empty from full. */
    for (uint8_t i = 0u; i < PM_EVTQ_DEPTH + 4u; i++) {
        pm_evtq_push(&q, PM_EVT_UART_ERROR, i, 0u);
    }
    CHECK(pm_evtq_dropped(&q) == 5u);
    {
        uint8_t n = 0u;
        while (pm_evtq_pop(&q, &e)) {
            n++;
        }
        CHECK(n == PM_EVTQ_DEPTH - 1u);                      /* one slot reserved */
    }
}

/* ----------------------------------------------------------------- gpio */

static void test_gpio(void)
{
    pm_gpio_pin_t pin0;
    pm_gpio_pin_t bad;
    pm_gpio_level_t level;
    pm_gpio_cfg_t cfg;

    mock_reset();
    pin0.id = 0u;
    bad.id  = 99u;

    memset(&cfg, 0, sizeof(cfg));
    cfg.dir = PM_GPIO_DIR_OUT;
    cfg.initial_level = PM_GPIO_LEVEL_HIGH;

    CHECK(pm_gpio_configure(pin0, NULL) == PM_ERR_NULL);
    CHECK(pm_gpio_configure(bad, &cfg) == PM_ERR_RANGE);
    CHECK(pm_gpio_configure(pin0, &cfg) == PM_OK);
    CHECK(mock_gpio_is_configured(0u) == 1);
    CHECK(mock_gpio_level(0u) == PM_GPIO_LEVEL_HIGH);

    CHECK(pm_gpio_write(pin0, PM_GPIO_LEVEL_LOW) == PM_OK);
    CHECK(pm_gpio_read(pin0, &level) == PM_OK);
    CHECK(level == PM_GPIO_LEVEL_LOW);

    CHECK(pm_gpio_toggle(pin0) == PM_OK);
    CHECK(pm_gpio_read(pin0, &level) == PM_OK);
    CHECK(level == PM_GPIO_LEVEL_HIGH);

    CHECK(pm_gpio_read(pin0, NULL) == PM_ERR_NULL);
    CHECK(pm_gpio_read(bad, &level) == PM_ERR_RANGE);
    CHECK(level == PM_GPIO_LEVEL_LOW);      /* output defined on error */
}

/* ----------------------------------------------------------------- uart */

static pm_uart_t s_uart;
static uint8_t   s_rx[64];
static uint8_t   s_tx[64];

static void make_cfg(pm_uart_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->hw_id      = 1u;
    cfg->baudrate   = 115200u;
    cfg->parity     = PM_UART_PARITY_NONE;
    cfg->stop_bits  = PM_UART_STOP_1;
    cfg->data_bits  = 8u;
    cfg->rx_storage = s_rx;
    cfg->rx_size    = (uint16_t)sizeof(s_rx);
    cfg->tx_storage = s_tx;
    cfg->tx_size    = (uint16_t)sizeof(s_tx);
}

static int s_cb_count;
static pm_evt_id_t s_last_evt;

static void on_event(pm_uart_t *u, const pm_evt_t *evt, void *ctx)
{
    (void)u;
    (void)ctx;
    s_cb_count++;
    s_last_evt = evt->id;
}

static void test_uart_validation(void)
{
    pm_uart_cfg_t cfg;

    mock_reset();
    memset(&s_uart, 0, sizeof(s_uart));
    make_cfg(&cfg);

    CHECK(pm_uart_init(NULL, &cfg) == PM_ERR_NULL);
    CHECK(pm_uart_init(&s_uart, NULL) == PM_ERR_NULL);

    cfg.baudrate = 0u;
    CHECK(pm_uart_init(&s_uart, &cfg) == PM_ERR_PARAM);

    make_cfg(&cfg);
    cfg.data_bits = 9u;
    CHECK(pm_uart_init(&s_uart, &cfg) == PM_ERR_PARAM);

    make_cfg(&cfg);
    cfg.rx_size = 48u;                       /* not a power of two */
    CHECK(pm_uart_init(&s_uart, &cfg) == PM_ERR_PARAM);

    make_cfg(&cfg);
    cfg.hw_id = 9u;
    CHECK(pm_uart_init(&s_uart, &cfg) == PM_ERR_RANGE);

    make_cfg(&cfg);
    CHECK(pm_uart_init(&s_uart, &cfg) == PM_OK);
    CHECK(mock_uart_is_open(1u) == 1);

    /* A second init on the same hardware instance is a lifecycle error. */
    CHECK(pm_uart_init(&s_uart, &cfg) == PM_ERR_BUSY);

    CHECK(pm_uart_deinit(&s_uart) == PM_OK);
    CHECK(mock_uart_is_open(1u) == 0);
    CHECK(pm_uart_deinit(&s_uart) == PM_OK);     /* idempotent */
}

static void test_uart_tx(void)
{
    pm_uart_cfg_t cfg;
    pm_uart_stats_t stats;
    pm_evt_t evt;
    size_t written = 0u;
    static const uint8_t msg[] = "hello";

    mock_reset();
    memset(&s_uart, 0, sizeof(s_uart));
    host_time_reset();
    s_cb_count = 0;
    make_cfg(&cfg);

    CHECK(pm_uart_init(&s_uart, &cfg) == PM_OK);
    CHECK(pm_uart_write(&s_uart, msg, sizeof(msg) - 1u, &written,
                        PM_UART_TIMEOUT_NOW) == PM_OK);
    CHECK(written == 5u);
    CHECK(mock_uart_tx_len() == 5u);
    CHECK(memcmp(mock_uart_tx_data(), "hello", 5u) == 0);

    /* Nothing is consumed from the ring until the TX interrupt fires. */
    CHECK(pm_uart_get_stats(&s_uart, &stats) == PM_OK);
    CHECK(stats.tx_bytes == 0u);

    pm_uart_hw_tx_complete(&s_uart);
    CHECK(pm_uart_get_stats(&s_uart, &stats) == PM_OK);
    CHECK(stats.tx_bytes == 5u);

    CHECK(pm_uart_poll(&s_uart, &evt, NULL, NULL) == true);
    CHECK(evt.id == PM_EVT_UART_TX_DONE);
    CHECK(pm_uart_poll(&s_uart, &evt, NULL, NULL) == false);

    /* Callback path: runs on poll, never in the "ISR". */
    (void)pm_uart_write(&s_uart, msg, 1u, NULL, PM_UART_TIMEOUT_NOW);
    pm_uart_hw_tx_complete(&s_uart);
    CHECK(s_cb_count == 0);
    CHECK(pm_uart_poll(&s_uart, NULL, on_event, NULL) == true);
    CHECK(s_cb_count == 1);
    CHECK(s_last_evt == PM_EVT_UART_TX_DONE);
}

static void test_uart_rx(void)
{
    pm_uart_cfg_t cfg;
    pm_uart_stats_t stats;
    pm_evt_t evt;
    uint8_t buf[8];
    size_t got = 0u;

    mock_reset();
    memset(&s_uart, 0, sizeof(s_uart));
    make_cfg(&cfg);
    CHECK(pm_uart_init(&s_uart, &cfg) == PM_OK);

    CHECK(pm_uart_read(&s_uart, buf, sizeof(buf), &got) == PM_ERR_AGAIN);
    CHECK(got == 0u);

    pm_uart_hw_rx_byte(&s_uart, 'A');      /* pretending to be the RX ISR */
    pm_uart_hw_rx_byte(&s_uart, 'B');

    CHECK(pm_uart_poll(&s_uart, &evt, NULL, NULL) == true);
    CHECK(evt.id == PM_EVT_UART_RX_DATA);
    CHECK(pm_uart_poll(&s_uart, &evt, NULL, NULL) == false);   /* de-duplicated */

    CHECK(pm_uart_read(&s_uart, buf, sizeof(buf), &got) == PM_OK);
    CHECK(got == 2u);
    CHECK(buf[0] == 'A');
    CHECK(buf[1] == 'B');

    CHECK(pm_uart_get_stats(&s_uart, &stats) == PM_OK);
    CHECK(stats.rx_bytes == 2u);
    CHECK(stats.rx_dropped == 0u);

    CHECK(pm_uart_flush_rx(&s_uart) == PM_OK);
    CHECK(pm_uart_read(&s_uart, buf, sizeof(buf), &got) == PM_ERR_AGAIN);
}

static void test_uart_timeout(void)
{
    pm_uart_cfg_t cfg;
    pm_uart_stats_t stats;
    uint8_t big[200];
    size_t written = 0u;

    memset(big, 0x5A, sizeof(big));
    mock_reset();
    memset(&s_uart, 0, sizeof(s_uart));
    host_time_reset();
    make_cfg(&cfg);

    CHECK(pm_uart_init(&s_uart, &cfg) == PM_OK);

    /* Ring capacity is 63 bytes after reserving one slot. */
    CHECK(pm_uart_write(&s_uart, big, sizeof(big), &written,
                        PM_UART_TIMEOUT_NOW) == PM_ERR_AGAIN);
    CHECK(written == 63u);

    CHECK(pm_uart_get_stats(&s_uart, &stats) == PM_OK);
    CHECK(stats.tx_dropped == 200u - 63u);

    /* Blocking form: the mock never completes TX, so it must time out. */
    CHECK(pm_uart_write(&s_uart, big, sizeof(big), &written, 20u) == PM_ERR_TIMEOUT);
    CHECK(written == 0u);
}

int main(void)
{
    test_ringbuf();
    test_evtq();
    test_gpio();
    test_uart_validation();
    test_uart_tx();
    test_uart_rx();
    test_uart_timeout();

    printf("\n%d checks, %d failure(s)\n", s_checks, s_failures);
    return (s_failures == 0) ? 0 : 1;
}
