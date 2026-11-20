/*
 * app_wiring.h - application command wiring.
 *
 * The command table and the thin adapters that bind component entry points to
 * the pm_cmd_entry_t signature live here; src/app/main.c only installs the
 * executor and sequences initialization. Moved out of main.c by split S10.
 */
#ifndef APP_WIRING_H
#define APP_WIRING_H

#include "pm_cmd_out.h"

/* Routes one command line through the application command table. */
void app_cmd_execute(cmd_port_t *c, const char *line);

#endif /* APP_WIRING_H */
