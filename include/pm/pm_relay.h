/*
 * pm_relay.h - single-pole relay driven by one GPIO.
 *
 * Device-level model: it composes pm_gpio and needs no new port primitive.
 * The only logic is the polarity map and the fail-safe rule below.
 *
 * Contract (docs/00-contract.md section 22):
 *  - `active_level` is mandatory. PM_RELAY_ACTIVE_UNSET (the value a
 *    zero-initialized config carries) is rejected with PM_ERR_PARAM; the
 *    model never guesses a polarity.
 *  - init() leaves the relay de-energised, i.e. at the inactive level. A
 *    caller that wants it closed must call pm_relay_on() explicitly.
 *  - deinit() drives the inactive level again before forgetting the pin, so
 *    teardown cannot leave a load energised.
 */
#ifndef PM_RELAY_H
#define PM_RELAY_H

#include "pm_common.h"
#include "pm_err.h"
#include "pm_port.h"
#include <stdbool.h>

typedef enum {
    PM_RELAY_ACTIVE_UNSET = 0,   /* invalid: "the caller did not decide" */
    PM_RELAY_ACTIVE_LOW   = 1,
    PM_RELAY_ACTIVE_HIGH  = 2
} pm_relay_polarity_t;

typedef struct {
    pm_gpio_pin_t       pin;          /* logical pin, board-defined */
    pm_relay_polarity_t active_level; /* PM_RELAY_ACTIVE_LOW or _HIGH */
} pm_relay_cfg_t;

typedef struct pm_relay pm_relay_t;

struct pm_relay {
    uint32_t magic;
    uint8_t  state;
    uint8_t  on;
    uint8_t  reserved[2];

    pm_gpio_pin_t       pin;
    pm_relay_polarity_t active_level;
};

/* Configures the pin as an output at the inactive level. Returns PM_ERR_PARAM
 * for an unset polarity, PM_ERR_RANGE for an unknown pin, PM_ERR_BUSY when the
 * object is already open. */
pm_err_t pm_relay_init(pm_relay_t *r, const pm_relay_cfg_t *cfg);

/* Drives the inactive level and releases the model. Idempotent. */
pm_err_t pm_relay_deinit(pm_relay_t *r);

pm_err_t pm_relay_on(pm_relay_t *r);
pm_err_t pm_relay_off(pm_relay_t *r);
pm_err_t pm_relay_set(pm_relay_t *r, bool on);
pm_err_t pm_relay_toggle(pm_relay_t *r);

/* False for an uninitialised object. */
bool pm_relay_is_on(const pm_relay_t *r);

#endif /* PM_RELAY_H */
