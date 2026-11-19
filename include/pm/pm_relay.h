/*
 * pm_relay.h - relay output on the KEYSKING learning board.
 *
 * PB5 drives Q3 (SS8050) which energises the SRD-05VDC-SL-C coil; D2 is the
 * flyback diode and R9 the base resistor. The contract only exposes "on"
 * (coil energised, PB5 high) and "off"; whether a click is audible and which
 * contact closes must be checked on the board.
 */
#ifndef PM_RELAY_H
#define PM_RELAY_H

#include <stdint.h>

/* PB5 as push-pull output, driven low at init (coil off). Returns 0. */
int pm_relay_init(void);

/* 1 = coil energised (PB5 high), 0 = released. */
void pm_relay_set(int on);

/* Actual PB5 output level, read back from the port register. */
uint32_t pm_relay_get(void);

#endif /* PM_RELAY_H */
