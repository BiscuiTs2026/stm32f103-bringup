/*
 * sys_cmds.h - small system commands: HELP, PING, STAT, I2C.
 *
 * Moved out of src/app/main.c by split step S10. The command table itself
 * lives in src/app/app_wiring.c; HELP takes it as a parameter so this
 * component never depends on the entry layer.
 */
#ifndef SYS_CMDS_H
#define SYS_CMDS_H

#include <stddef.h>

#include "pm_cmd_out.h"
#include "pm_cmd_table.h"

/* Prints one "HELP <text>" line per table entry, in table order. */
void sys_cmd_help(cmd_port_t *c, const pm_cmd_entry_t *t, size_t n);

/* PING -> "PONG". */
void sys_cmd_ping(cmd_port_t *c);

/* STAT -> clk/up/tick/keys/uart counters. */
void sys_cmd_stat(cmd_port_t *c);

/* I2C -> "I2C found=" plus the 8-bit write addresses the bus scan saw. */
void sys_cmd_i2c(cmd_port_t *c);

#endif /* SYS_CMDS_H */
