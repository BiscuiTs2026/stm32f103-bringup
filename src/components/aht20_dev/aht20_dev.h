/*
 * aht20_dev.h - AHT20 temperature/humidity job.
 *
 * The 80 ms conversion window runs from the main loop, never from the command
 * handler: a command must never block the heartbeat or the other port, and it
 * must always produce exactly one reply.
 *
 * Wire order from S4 onward: the trigger ends the command with STOP, then the
 * result is read with a plain START after AHT_WAIT_MS. `repeated` (AHT = 0,
 * AHTS = 1) only changes the rst= field of the reply, not the bus sequence.
 */
#ifndef AHT20_DEV_H
#define AHT20_DEV_H

#include <stdint.h>

#include "pm_cmd_out.h"

/* Entry-layer hooks. ui_busy returns non-zero while the screen owns the bus
 * for a multi-page push; ui_now_row repaints the NOW row while the sensor
 * converts and may be NULL. */
typedef struct {
    int  (*ui_busy)(void);
    void (*ui_now_row)(void);
} aht20_hooks_t;

void aht20_init(const aht20_hooks_t *h);
void aht20_start(cmd_port_t *c, int repeated);   /* AHT=0, AHTS=1; only affects rst= */
void aht20_service(uint32_t now);                /* >=80 ms later: plain START read, retrigger on failure, 3 tries */
void aht20_bg_tick(uint32_t now);                /* one background trigger every 2 s, no reply port */
int  aht20_busy(void);
int  aht20_reading(int32_t *rh10, int32_t *t10);

#endif
