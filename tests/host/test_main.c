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
#include "pm_cmd.h"
#include "pm_cmd_table.h"
#include "pm_port_mock.h"
#include "pm_timebase.h"

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

static void test_cmd(void)
{
    const char *rest = NULL;
    uint32_t v = 0u;
    char buf[16];

    CHECK(pm_cmd_ci_equal("HELP", "help") == 1);
    CHECK(pm_cmd_ci_equal("oled", "OLED") == 1);
    CHECK(pm_cmd_ci_equal("HELP", "HEL") == 0);
    CHECK(pm_cmd_ci_equal("HELP ", "HELP") == 0);
    CHECK(pm_cmd_ci_equal("", "") == 1);

    CHECK(pm_cmd_ci_starts("OLED col 3", "oled ", &rest) == 1);
    CHECK(strcmp(rest, "col 3") == 0);
    CHECK(pm_cmd_ci_starts("OLED", "oled ", &rest) == 0);
    CHECK(pm_cmd_ci_starts("PING", "ping", &rest) == 1);
    CHECK(*rest == '\0');

    CHECK((pm_cmd_parse_u32(" 42 ", &v) == 1) && (v == 42u));
    CHECK((pm_cmd_parse_u32("0", &v) == 1) && (v == 0u));
    CHECK(pm_cmd_parse_u32("", &v) == 0);
    CHECK(pm_cmd_parse_u32("   ", &v) == 0);
    CHECK(pm_cmd_parse_u32("4 2", &v) == 0);
    CHECK(pm_cmd_parse_u32("12a", &v) == 0);
    CHECK(pm_cmd_parse_u32("-1", &v) == 0);
    /* Golden vector for the carried-over behaviour: no overflow check, so
     * 2^32 wraps to 0. Pinned here, not changed in S1. */
    CHECK((pm_cmd_parse_u32("4294967296", &v) == 1) && (v == 0u));

    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_u32(buf, 0u) == &buf[1]);
    CHECK(buf[0] == '0');
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_u32(buf, 4294967295u) == &buf[10]);
    CHECK(memcmp(buf, "4294967295", 10) == 0);

    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_s(buf, "ab", 4u) == &buf[4]);
    CHECK(memcmp(buf, "ab  ", 4) == 0);
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_s(buf, "abcdef", 3u) == &buf[3]);
    CHECK(memcmp(buf, "abc", 3) == 0);

    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_pad(buf, 7u, 4u) == &buf[4]);
    CHECK(memcmp(buf, "   7", 4) == 0);
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_pad(buf, 1234u, 2u) == &buf[4]);
    CHECK(memcmp(buf, "1234", 4) == 0);
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_pad(buf, 1234u, 4u) == &buf[4]);   /* exactly full */
    CHECK(memcmp(buf, "1234", 4) == 0);
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_pad(buf, 0u, 1u) == &buf[1]);      /* width == 1 */
    CHECK(buf[0] == '0');

    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_f1(buf, 473) == &buf[4]);
    CHECK(memcmp(buf, "47.3", 4) == 0);
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_f1(buf, 95) == &buf[4]);
    CHECK(memcmp(buf, " 9.5", 4) == 0);
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_f1(buf, -50) == &buf[4]);
    CHECK(memcmp(buf, "-5.0", 4) == 0);
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_f1(buf, -500) == &buf[4]);
    CHECK(memcmp(buf, "-9.9", 4) == 0);
    memset(buf, '.', sizeof(buf));
    CHECK(pm_cmd_sput_f1(buf, 999) == &buf[4]);
    CHECK(memcmp(buf, "99.9", 4) == 0);
}

/* ------------------------------------------------------------- cmd table */

static int         s_exact_runs;
static int         s_arg_runs;
static const char *s_arg_last;

static void table_run(cmd_port_t *c, const char *line)
{
    (void)c;
    (void)line;
    s_exact_runs++;
}

/* Deliberately also answers the EXACT token: pass 1 must win. */
static const char *table_try(const char *line)
{
    if ((pm_cmd_ci_equal(line, "EXA") != 0) ||
        (pm_cmd_ci_equal(line, "TRY") != 0)) {
        return "T\r\n";
    }
    return NULL;
}

/* Also answers "TRY": pass 2 must win over pass 3. */
static const char *table_match(const char *line)
{
    if (pm_cmd_ci_equal(line, "TRY") != 0) {
        return "try";
    }
    if (pm_cmd_ci_equal(line, "PRE 7") != 0) {
        return "7";
    }
    return NULL;
}

static void table_run_arg(cmd_port_t *c, const char *arg)
{
    (void)c;
    s_arg_runs++;
    s_arg_last = arg;
}

static const pm_cmd_entry_t s_test_table[] = {
    { "EXA", "EXA=x", PM_CMD_EXACT,  table_run, NULL,        NULL,        NULL },
    { "TRY", "TRY=x", PM_CMD_TRY,    NULL,      table_try,   NULL,        NULL },
    { "PRE", "PRE=x", PM_CMD_PREFIX, NULL,      NULL,        table_match, table_run_arg },
};

static pm_uart_t s_tbl_uart;
static uint8_t   s_tbl_rx[64];
static uint8_t   s_tbl_tx[256];

static void table_port_init(cmd_port_t *c)
{
    pm_uart_cfg_t cfg;

    mock_reset();
    memset(&s_tbl_uart, 0, sizeof(s_tbl_uart));
    memset(&cfg, 0, sizeof(cfg));
    cfg.hw_id      = 1u;
    cfg.baudrate   = 115200u;
    cfg.parity     = PM_UART_PARITY_NONE;
    cfg.stop_bits  = PM_UART_STOP_1;
    cfg.data_bits  = 8u;
    cfg.rx_storage = s_tbl_rx;
    cfg.rx_size    = (uint16_t)sizeof(s_tbl_rx);
    cfg.tx_storage = s_tbl_tx;
    cfg.tx_size    = (uint16_t)sizeof(s_tbl_tx);
    (void)pm_uart_init(&s_tbl_uart, &cfg);

    memset(c, 0, sizeof(*c));
    c->port = &s_tbl_uart;
}

static void test_cmd_table(void)
{
    cmd_port_t     c;
    pm_cmd_entry_t bad[3];
    char           buf[32];
    size_t         base;

    CHECK(pm_cmd_table_validate(s_test_table, 3) == 0);

    /* Help lines: "HELP " + help + CRLF, in table order, with truncation. */
    CHECK(pm_cmd_help_line(s_test_table, 3, 0, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "HELP EXA=x\r\n") == 0);
    CHECK(pm_cmd_help_line(s_test_table, 3, 2, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "HELP PRE=x\r\n") == 0);
    CHECK(pm_cmd_help_line(s_test_table, 3, 3, buf, sizeof(buf)) == -1);
    CHECK(pm_cmd_help_line(s_test_table, 3, 0, buf, 12u) == -1);  /* needs 12 + NUL */
    CHECK(pm_cmd_help_line(s_test_table, 3, 0, buf, 13u) == 0);

    /* Validation: duplicate name, empty help, mixed-case name, kind mismatch. */
    memcpy(bad, s_test_table, sizeof(bad));
    bad[1].name = "EXA";
    CHECK(pm_cmd_table_validate(bad, 3) == 1);
    memcpy(bad, s_test_table, sizeof(bad));
    bad[0].help = "";
    CHECK(pm_cmd_table_validate(bad, 3) == 1);
    memcpy(bad, s_test_table, sizeof(bad));
    bad[2].name = "Pre";
    CHECK(pm_cmd_table_validate(bad, 3) == 1);
    memcpy(bad, s_test_table, sizeof(bad));
    bad[1].run = table_run;
    CHECK(pm_cmd_table_validate(bad, 3) == 1);

    table_port_init(&c);
    s_exact_runs = 0;
    s_arg_runs = 0;
    s_arg_last = NULL;

    /* EXACT beats TRY even though the TRY handler also answers "exa". */
    base = mock_uart_tx_len();
    pm_cmd_dispatch(s_test_table, 3, &c, "exa");
    CHECK(s_exact_runs == 1);
    CHECK(mock_uart_tx_len() == base);            /* nothing written */

    /* TRY beats PREFIX on the same token, and its reply is sent. */
    pm_uart_hw_tx_complete(&s_tbl_uart);
    base = mock_uart_tx_len();
    pm_cmd_dispatch(s_test_table, 3, &c, "TRY");
    CHECK(s_exact_runs == 1);
    CHECK(s_arg_runs == 0);
    CHECK(mock_uart_tx_len() - base == 3u);
    CHECK(memcmp(mock_uart_tx_data() + base, "T\r\n", 3u) == 0);

    /* PREFIX pass runs last and hands the argument to run_arg. */
    pm_uart_hw_tx_complete(&s_tbl_uart);
    pm_cmd_dispatch(s_test_table, 3, &c, "pre 7");
    CHECK(s_arg_runs == 1);
    CHECK(strcmp(s_arg_last, "7") == 0);

    /* Miss: the original line is echoed verbatim. The three tx_str calls queue
     * behind one another, so drain the mock TX path before checking. */
    pm_uart_hw_tx_complete(&s_tbl_uart);
    base = mock_uart_tx_len();
    pm_cmd_dispatch(s_test_table, 3, &c, "nope");
    while (mock_uart_tx_len() < (base + strlen("ERR unknown=nope\r\n"))) {
        size_t before = mock_uart_tx_len();

        pm_uart_hw_tx_complete(&s_tbl_uart);
        if (mock_uart_tx_len() == before) {
            break;
        }
    }
    CHECK(mock_uart_tx_len() - base == strlen("ERR unknown=nope\r\n"));
    CHECK(memcmp(mock_uart_tx_data() + base, "ERR unknown=nope\r\n",
                 strlen("ERR unknown=nope\r\n")) == 0);
    CHECK(s_exact_runs == 1);
    CHECK(s_arg_runs == 1);
}

/* -------------------------------------------------------------- timebase */

/* pm_timebase_ok() lives in src/port/stm32f1/pm_timebase.h but touches no
 * register; it is compiled here to turn the -O2 constant fold of round 2/4
 * into an executed check. 72 MHz, 10 permille tolerance. */
static void test_timebase(void)
{
    /* The pairs the firmware actually writes. */
    CHECK(pm_timebase_ok(72000000u, 71u, 19999u, 50u, 10u) == 1);
    CHECK(pm_timebase_ok(72000000u, 0u, 3599u, 20000u, 10u) == 1);
    /* Same pairs against the wrong target frequency. */
    CHECK(pm_timebase_ok(72000000u, 71u, 19999u, 20000u, 10u) == 0);
    CHECK(pm_timebase_ok(72000000u, 0u, 3599u, 50u, 10u) == 0);
    CHECK(pm_timebase_ok(72000000u, 0u, 3599u, 1000u, 10u) == 0);
    /* The 3.6 kHz accident: PSC 0 instead of 71. */
    CHECK(pm_timebase_ok(72000000u, 0u, 19999u, 50u, 10u) == 0);
    /* The model's computed proposal is legal, just not used. */
    CHECK(pm_timebase_ok(72000000u, 21u, 65453u, 50u, 10u) == 1);
    /* Boundary: 71/19990 is 450 ppm off. The *rounded* frequency is still
     * 50 Hz, so a rounded comparison would wrongly accept it at tol 0; the
     * cross-multiplied diff must reject it, and accept it only once the
     * tolerance really covers the error. */
    CHECK(pm_timebase_ok(72000000u, 71u, 19990u, 50u, 0u) == 0);
    CHECK(pm_timebase_ok(72000000u, 71u, 19990u, 50u, 1u) == 1);
    /* Symmetry: 71/20009 is 500 ppm slow, must be judged the same way. */
    CHECK(pm_timebase_ok(72000000u, 71u, 20009u, 50u, 0u) == 0);
    CHECK(pm_timebase_ok(72000000u, 71u, 20009u, 50u, 1u) == 1);
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
    test_cmd();
    test_cmd_table();
    test_timebase();

    printf("\n%d checks, %d failure(s)\n", s_checks, s_failures);
    return (s_failures == 0) ? 0 : 1;
}
