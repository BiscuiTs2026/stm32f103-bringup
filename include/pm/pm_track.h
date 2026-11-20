/*
 * pm_track.h - line-tracking sensor input (one digital channel).
 *
 * The module is deliberately tiny: a handle, init/read_raw and the polarity
 * decision in the core so the host tests can exercise it. The port only
 * reads one GPIO.
 *
 * Contract:
 *  - `active_level` is the electrical level that means "sensor active"
 *    (usually "on the line"). It is a configuration value, not a guess made
 *    in the driver.
 *  - The port configures the pin as input with the pull the board needs
 *    (PB14 uses the internal pull-up) and does no filtering. Debouncing, if
 *    needed, belongs to the caller.
 *  - On this board PB14 is also SPI2_MISO: pm_track and SPI2 must not be
 *    open at the same time (docs/00-contract.md section 18).
 */
#ifndef PM_TRACK_H
#define PM_TRACK_H

#include "pm_common.h"
#include "pm_err.h"
#include "pm_port.h"

struct pm_track {
    uint32_t magic;
    uint8_t  state;
    uint8_t  hw_id;
    uint8_t  active_level;
    uint8_t  reserved;
    const pm_track_ops_t *ops;
    void                 *port_ctx;
};

/* Opens the sensor pin. Returns PM_ERR_PARAM for an invalid level,
 * PM_ERR_RANGE for an unknown hw_id, PM_ERR_BUSY when already open and
 * PM_ERR_CONFLICT when the pin is owned by SPI2. */
pm_err_t pm_track_init(pm_track_t *t, const pm_track_cfg_t *cfg);

/* Releases the pin. Idempotent. */
pm_err_t pm_track_deinit(pm_track_t *t);

/* One sample: `on_line` is true when the pin level equals active_level. */
pm_err_t pm_track_read_raw(pm_track_t *t, bool *on_line);

#endif /* PM_TRACK_H */
