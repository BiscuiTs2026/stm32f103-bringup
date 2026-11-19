/*
 * pm_cmd_out.h - command reply output layer.
 *
 * Every reply goes back to the port its command arrived on. The BLE port is
 * additionally mirrored onto the debug port, and the LAST summary / TX byte
 * counter shared with the status pages live here.
 */
#ifndef PM_CMD_OUT_H
#define PM_CMD_OUT_H

#include <stddef.h>
#include <stdint.h>

#include "pm_uart.h"

/* Command channel state; the parser itself lives in the entry glue. */
#define CMD_LINE_MAX 48u

typedef struct {
    pm_uart_t *port;
    char       line[CMD_LINE_MAX];
    size_t     len;
    uint32_t   last_ms;    /* last byte seen on this port, for idle flush */
} cmd_port_t;

/* Registers the debug port and the port whose replies are mirrored onto it.
 * Call once after the debug UART is initialized. */
void pm_cmd_out_init(pm_uart_t *debug, pm_uart_t *mirror);

/* A full reply, pushed until every byte is accepted (1000 ms per attempt). */
void pm_cmd_out_tx_bytes_all(cmd_port_t *c, const uint8_t *d, size_t n);
void pm_cmd_out_tx_str(cmd_port_t *c, const char *s);
void pm_cmd_out_tx_u32(cmd_port_t *c, uint32_t v);
void pm_cmd_out_tx_hex_byte(cmd_port_t *c, uint8_t b);
void pm_cmd_out_tx_fixed1(cmd_port_t *c, int32_t v);

/* Debug-port writes (50 ms per attempt). */
void pm_cmd_out_uart_write_str(const char *s);
void pm_cmd_out_uart_write_u32(uint32_t v);
void pm_cmd_out_uart_write_hex(const uint8_t *d, size_t n);

/* LAST summary, capped at 15 characters. */
void pm_cmd_out_last_set(const char *s);
void pm_cmd_out_last_num(const char *pre, uint32_t v, const char *suf);
void pm_cmd_out_last_two(const char *a, const char *b);
const char *pm_cmd_out_last(void);

/* TX byte counter reported by STAT. */
uint32_t pm_cmd_out_tx_total(void);

#endif /* PM_CMD_OUT_H */
