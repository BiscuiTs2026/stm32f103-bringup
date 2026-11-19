/*
 * oled_ui.h - CH1116 screen job state machine (component layer).
 *
 * Moved out of src/app/main.c in S9b. The entry layer only boots it, ticks it
 * and wires the command registry and the AHT hooks to it.
 *
 * DIST still lives in the entry layer until S6 moves it to sonar_dev, so the
 * two DIST interactions are injected as hooks instead of being called directly.
 */
#ifndef OLED_UI_H
#define OLED_UI_H

#include <stdint.h>

#include "pm_cmd_out.h"

typedef struct {
    int  (*dist_busy)(void);                  /* a DIST measurement is in flight */
    void (*dist_live_poke)(cmd_port_t *port); /* start one live measurement */
} oled_ui_hooks_t;

/* Init sequence, synchronous clear, then arms the default live refresh. */
void oled_ui_boot(const oled_ui_hooks_t *h, cmd_port_t *live_port);

/* At most one page per call; call once per main loop iteration. */
void oled_ui_service(void);

/* 1 Hz ranging plus the 500 ms refresh; call once per main loop iteration. */
void oled_ui_live_tick(uint32_t now);

/* 1 while a multi-page job owns the panel (used by the AHT20 job to back off). */
int oled_ui_busy(void);

/* Repaint the NOW row for a short job; safe outside a panel job. */
void oled_ui_now_row(void);

/* Command table hooks: arg matcher and the OLED sub-command handler. */
const char *oled_ui_arg(const char *line);
void        oled_ui_cmd(cmd_port_t *c, const char *arg);

#endif /* OLED_UI_H */
