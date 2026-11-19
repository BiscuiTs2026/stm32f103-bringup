/*
 * pm_track.h - line-tracking sensor input on the KEYSKING learning board.
 *
 * One TCRT5000 style module, digital output on PB14. The contract exposes the
 * raw pin level only; which colour that level means depends on the module and
 * is not assumed here.
 */
#ifndef PM_TRACK_H
#define PM_TRACK_H

#include <stdint.h>

/* PB14 as input with pull-up. Returns 0 on success. */
int pm_track_init(void);

/* Raw level on the sensor output: 0 or 1, straight from the pin. */
uint32_t pm_track_raw(void);

#endif /* PM_TRACK_H */
