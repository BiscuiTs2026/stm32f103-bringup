/*
 * pm_cmd_out.c - command reply output layer.
 *
 * Replies go back to the port the command arrived on; the BLE port is also
 * mirrored onto the debug port so one capture shows both directions of the
 * USART3 path. The LAST summary and the TX byte counter live here as well.
 */
#include "pm_cmd_out.h"
#include "pm_cmd.h"

#include <string.h>

static pm_uart_t *s_debug_port;
static pm_uart_t *s_mirror_port;
static uint32_t   s_tx_total;

/* Last command and result, shown on the live page as "LAST: ". Capped at 15
 * characters so it fits after the 6-column prefix; longer replies are cut. */
static char s_last[16] = "boot";

void pm_cmd_out_init(pm_uart_t *debug, pm_uart_t *mirror)
{
    s_debug_port  = debug;
    s_mirror_port = mirror;
}

const char *pm_cmd_out_last(void)
{
    return s_last;
}

uint32_t pm_cmd_out_tx_total(void)
{
    return s_tx_total;
}

void pm_cmd_out_last_set(const char *s)
{
    size_t n = 0u;

    while ((s[n] != '\0') && (n < (sizeof(s_last) - 1u))) {
        s_last[n] = s[n];
        n++;
    }
    s_last[n] = '\0';
}

void pm_cmd_out_last_num(const char *pre, uint32_t v, const char *suf)
{
    char  b[16];
    char *p = b;

    while (*pre != '\0') {
        *p++ = *pre++;
    }
    p = pm_cmd_sput_u32(p, v);
    while (*suf != '\0') {
        *p++ = *suf++;
    }
    *p = '\0';
    pm_cmd_out_last_set(b);
}

void pm_cmd_out_last_two(const char *a, const char *b)
{
    char        buf[16];
    char       *p = buf;
    const char *s;

    for (s = a; (*s != '\0') && (p < &buf[15]); s++) {
        *p++ = *s;
    }
    for (s = b; (*s != '\0') && (p < &buf[15]); s++) {
        *p++ = *s;
    }
    *p = '\0';
    pm_cmd_out_last_set(buf);
}

void pm_cmd_out_uart_write_str(const char *s)
{
    size_t n = strlen(s);

    /* Bounded, but not zero: with PM_UART_TIMEOUT_NOW the tail of a long line
     * is dropped as soon as the ring is full, which silently corrupted the
     * BLE traffic mirror. 50 ms at 115200 is ~570 bytes. */
    (void)pm_uart_write(s_debug_port, (const uint8_t *)s, n, NULL, 50u);
    s_tx_total += (uint32_t)n;
}

void pm_cmd_out_uart_write_u32(uint32_t v)
{
    char  buf[11];
    char *p = &buf[sizeof(buf) - 1];

    *p = '\0';
    do {
        *--p = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    pm_cmd_out_uart_write_str(p);
}

void pm_cmd_out_uart_write_hex(const uint8_t *d, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    char   out[4];
    size_t i;

    for (i = 0u; i < n; i++) {
        out[0] = ' ';
        out[1] = hex[(d[i] >> 4) & 0x0Fu];
        out[2] = hex[d[i] & 0x0Fu];
        out[3] = '\0';
        pm_cmd_out_uart_write_str(out);
    }
}

void pm_cmd_out_tx_bytes_all(cmd_port_t *c, const uint8_t *d, size_t n)
{
    size_t off = 0u;

    while (off < n) {
        size_t   w  = 0u;
        pm_err_t rc = pm_uart_write(c->port, &d[off], n - off, &w, 1000u);

        off += w;
        if ((rc != PM_OK) && (w == 0u)) {
            break;
        }
    }
    s_tx_total += (uint32_t)off;

    /* Mirror what actually left the BLE port so a COM11 capture shows both
     * directions of the USART3 path, not just the RX side. */
    if (c->port == s_mirror_port) {
        pm_cmd_out_uart_write_str("BT tx n=");
        pm_cmd_out_uart_write_u32((uint32_t)off);
        pm_cmd_out_uart_write_str("B:");
        pm_cmd_out_uart_write_hex(d, off);
        pm_cmd_out_uart_write_str("\r\n");
    }
}

void pm_cmd_out_tx_str(cmd_port_t *c, const char *s)
{
    pm_cmd_out_tx_bytes_all(c, (const uint8_t *)s, strlen(s));
}

void pm_cmd_out_tx_u32(cmd_port_t *c, uint32_t v)
{
    char  buf[11];
    char *p = &buf[sizeof(buf) - 1];

    *p = '\0';
    do {
        *--p = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    pm_cmd_out_tx_str(c, p);
}

void pm_cmd_out_tx_hex_byte(cmd_port_t *c, uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    char out[3];

    out[0] = hex[(b >> 4) & 0x0Fu];
    out[1] = hex[b & 0x0Fu];
    out[2] = '\0';
    pm_cmd_out_tx_str(c, out);
}

void pm_cmd_out_tx_fixed1(cmd_port_t *c, int32_t v)
{
    if (v < 0) {
        pm_cmd_out_tx_str(c, "-");
        v = -v;
    }
    pm_cmd_out_tx_u32(c, (uint32_t)(v / 10));
    pm_cmd_out_tx_str(c, ".");
    pm_cmd_out_tx_u32(c, (uint32_t)(v % 10));
}
