/*
 * pm_cmd_console.h - console transport for the shared command parser.
 *
 * Owns the debug/BLE UART instances, the two command ports, line framing and
 * the idle flush. The parser itself stays in the entry glue: the entry layer
 * installs its executor through pm_cmd_console_t.execute, so this component
 * never calls back into glue code by name.
 */
#ifndef PM_CMD_CONSOLE_H
#define PM_CMD_CONSOLE_H

#include <stdint.h>

#include "pm_uart.h"
#include "pm_cmd_out.h"

typedef struct {
    /* Entry-layer line executor, installed before init. */
    void (*execute)(cmd_port_t *port, const char *line);

    pm_uart_t  *debug_uart;
    pm_uart_t  *bt_uart;
    cmd_port_t *debug_port;
    cmd_port_t *bt_port;
} pm_cmd_console_t;

/* Initializes the debug UART and fills the ports. Returns 0 on success,
 * non-zero when the debug UART failed (the entry layer goes to error_loop).
 * The BT UART is brought up by pm_cmd_console_init_bt(), which must be called
 * after pm_cmd_out_init() so its banner keeps the boot output order. */
int pm_cmd_console_init(pm_cmd_console_t *out, uint8_t debug_hw, uint8_t bt_hw);

/* Initializes the BLE UART and prints its banner on the debug port. A failure
 * only prints a message; the console keeps running. */
void pm_cmd_console_init_bt(void);

/* Drains and parses one port; call both once per main-loop pass. */
void pm_cmd_console_service_debug(void);
void pm_cmd_console_service_bt(void);

/* Executes a line that stopped growing for CMD_IDLE_MS on either port. */
void pm_cmd_console_flush_idle(uint32_t now);

/* Bytes received on both ports since boot, for the STAT reply. */
uint32_t pm_cmd_console_rx_total(void);

#endif /* PM_CMD_CONSOLE_H */
