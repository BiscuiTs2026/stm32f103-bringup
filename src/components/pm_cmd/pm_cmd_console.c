/*
 * pm_cmd_console.c - debug/BLE UART transport, line framing, idle flush.
 *
 * Moved out of main.c by S9c. The command table and its handlers stay in the
 * entry glue; this file only frames bytes into lines and hands them to the
 * executor installed in pm_cmd_console_t.
 */
#include "pm_common.h"
#include "pm_osal.h"
#include "pm_uart.h"
#include "pm_cmd_out.h"
#include "pm_cmd_console.h"

#include <string.h>

static pm_uart_t s_uart;
static uint8_t   s_rx_storage[256];
static uint8_t   s_tx_storage[512];

static uint32_t s_uart_errors;

static pm_uart_cfg_t s_uart_cfg;

/* Bluetooth module on USART3 (PB10/PB11): 9600 8N1, shared command parser. */
static pm_uart_t s_bt;
static uint8_t   s_bt_rx_storage[128];
static uint8_t   s_bt_tx_storage[512];

static pm_uart_cfg_t s_bt_cfg;

static cmd_port_t s_debug = { .port = &s_uart };
static cmd_port_t s_ble   = { .port = &s_bt };

static uint32_t s_rx_total;
static uint8_t  s_bt_hw;

static pm_cmd_console_t *s_self;

static void console_feed(cmd_port_t *c, const uint8_t *d, size_t n)
{
    size_t i;

    s_rx_total += (uint32_t)n;
    c->last_ms = pm_osal_now_ms();
    for (i = 0u; i < n; i++) {
        uint8_t b = d[i];

        if ((b == (uint8_t)'\r') || (b == (uint8_t)'\n')) {
            if (c->len != 0u) {
                c->line[c->len] = '\0';
                s_self->execute(c, c->line);
                c->len = 0u;
            }
            continue;
        }
        if ((b >= 0x20u) && (b < 0x7Fu)) {
            if (c->len < (CMD_LINE_MAX - 1u)) {
                c->line[c->len] = (char)b;
                c->len++;
            }
        }
    }
}

static void on_uart_event(pm_uart_t *u, const pm_evt_t *evt, void *ctx)
{
    PM_UNUSED(u);
    PM_UNUSED(ctx);

    if (evt->id == PM_EVT_UART_ERROR) {
        s_uart_errors++;
    }
}

int pm_cmd_console_init(pm_cmd_console_t *out, uint8_t debug_hw, uint8_t bt_hw)
{
    if (out == NULL) {
        return 1;
    }

    s_uart_cfg = (pm_uart_cfg_t){
        .hw_id      = debug_hw,
        .baudrate   = 115200u,
        .parity     = PM_UART_PARITY_NONE,
        .stop_bits  = PM_UART_STOP_1,
        .data_bits  = 8u,
        .rx_storage = s_rx_storage,
        .rx_size    = (uint16_t)sizeof(s_rx_storage),
        .tx_storage = s_tx_storage,
        .tx_size    = (uint16_t)sizeof(s_tx_storage),
    };
    if (pm_uart_init(&s_uart, &s_uart_cfg) != PM_OK) {
        return 1;
    }

    s_bt_hw = bt_hw;
    s_debug.port = &s_uart;
    s_ble.port   = &s_bt;

    out->debug_uart = &s_uart;
    out->bt_uart    = &s_bt;
    out->debug_port = &s_debug;
    out->bt_port    = &s_ble;
    s_self = out;
    return 0;
}

void pm_cmd_console_init_bt(void)
{
    s_bt_cfg = (pm_uart_cfg_t){
        .hw_id      = s_bt_hw,
        .baudrate   = 9600u,            /* DX-BT24 factory default */
        .parity     = PM_UART_PARITY_NONE,
        .stop_bits  = PM_UART_STOP_1,
        .data_bits  = 8u,
        .rx_storage = s_bt_rx_storage,
        .rx_size    = (uint16_t)sizeof(s_bt_rx_storage),
        .tx_storage = s_bt_tx_storage,
        .tx_size    = (uint16_t)sizeof(s_bt_tx_storage),
    };

    if (pm_uart_init(&s_bt, &s_bt_cfg) == PM_OK) {
        pm_cmd_out_uart_write_str("BT USART3 9600 8N1, command channel ready\r\n");
    } else {
        pm_cmd_out_uart_write_str("BT USART3 init FAILED\r\n");
    }
}

void pm_cmd_console_service_debug(void)
{
    uint8_t buf[32];
    size_t  got = 0u;

    while (pm_uart_read(&s_uart, buf, sizeof(buf), &got) == PM_OK) {
        if (got == 0u) {
            break;
        }
        console_feed(&s_debug, buf, got);
    }

    while (pm_uart_poll(&s_uart, NULL, on_uart_event, NULL)) {
        /* drain one event per call */
    }
}

void pm_cmd_console_service_bt(void)
{
    uint8_t buf[32];
    size_t  n = 0u;

    while (pm_uart_read(&s_bt, buf, sizeof(buf), &n) == PM_OK) {
        if (n == 0u) {
            break;
        }
        /* Mirror what the BLE side sent so a COM11 capture shows both paths. */
        pm_cmd_out_uart_write_str("BT rx n=");
        pm_cmd_out_uart_write_u32((uint32_t)n);
        pm_cmd_out_uart_write_str("B:");
        pm_cmd_out_uart_write_hex(buf, n);
        pm_cmd_out_uart_write_str("\r\n");
        console_feed(&s_ble, buf, n);
    }
}

/* A browser/Web Bluetooth client may send "AHT" without any CR/LF (the page's
 * line ending is configurable, and DX-BT24 forwards BLE packets verbatim).
 * Requiring a terminator left the token sitting in the buffer and produced no
 * reply at all. Any line that stops growing for CMD_IDLE_MS is executed. */
#define CMD_IDLE_MS 100u

static void console_flush_idle(cmd_port_t *c, uint32_t now)
{
    if ((c->len == 0u) || ((uint32_t)(now - c->last_ms) < CMD_IDLE_MS)) {
        return;
    }
    c->line[c->len] = '\0';
    c->len = 0u;
    s_self->execute(c, c->line);
}

void pm_cmd_console_flush_idle(uint32_t now)
{
    console_flush_idle(&s_debug, now);
    console_flush_idle(&s_ble, now);
}

uint32_t pm_cmd_console_rx_total(void)
{
    return s_rx_total;
}
