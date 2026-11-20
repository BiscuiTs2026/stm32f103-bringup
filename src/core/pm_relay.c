/*
 * pm_relay.c - polarity map and fail-safe level for a GPIO-driven relay.
 *
 * Hardware independent: all writes go through pm_gpio, the port layer.
 */
#include "pm_relay.h"
#include "pm_gpio.h"
#include <string.h>

#define PM_RELAY_STATE_UNINIT 0u
#define PM_RELAY_STATE_READY  1u
#define PM_RELAY_MAGIC        0x524C5931u   /* "RLY1" */

static pm_gpio_level_t active_of(pm_relay_polarity_t p)
{
    return (p == PM_RELAY_ACTIVE_LOW) ? PM_GPIO_LEVEL_LOW : PM_GPIO_LEVEL_HIGH;
}

static pm_gpio_level_t inactive_of(pm_relay_polarity_t p)
{
    return (p == PM_RELAY_ACTIVE_LOW) ? PM_GPIO_LEVEL_HIGH : PM_GPIO_LEVEL_LOW;
}

static pm_err_t relay_drive(pm_relay_t *r, pm_gpio_level_t level)
{
    return pm_gpio_write(r->pin, level);
}

pm_err_t pm_relay_init(pm_relay_t *r, const pm_relay_cfg_t *cfg)
{
    pm_gpio_cfg_t pin_cfg;
    pm_err_t rc;

    if ((r == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    /* An unset polarity is a caller bug, not a request for a default. */
    if ((cfg->active_level != PM_RELAY_ACTIVE_LOW) &&
        (cfg->active_level != PM_RELAY_ACTIVE_HIGH)) {
        return PM_ERR_PARAM;
    }
    if ((r->magic == PM_RELAY_MAGIC) && (r->state == PM_RELAY_STATE_READY)) {
        return PM_ERR_BUSY;
    }

    (void)memset(&pin_cfg, 0, sizeof(pin_cfg));
    pin_cfg.dir = PM_GPIO_DIR_OUT;
    pin_cfg.initial_level = inactive_of(cfg->active_level);

    rc = pm_gpio_configure(cfg->pin, &pin_cfg);
    if (rc != PM_OK) {
        return rc;
    }

    (void)memset(r, 0, sizeof(*r));
    r->pin          = cfg->pin;
    r->active_level = cfg->active_level;
    r->on           = 0u;
    r->state        = PM_RELAY_STATE_READY;
    r->magic        = PM_RELAY_MAGIC;
    return PM_OK;
}

pm_err_t pm_relay_deinit(pm_relay_t *r)
{
    if (r == NULL) {
        return PM_ERR_NULL;
    }
    if (r->state != PM_RELAY_STATE_READY) {
        return PM_OK;
    }

    /* Fail-safe: never leave the load energised on teardown. */
    (void)relay_drive(r, inactive_of(r->active_level));
    (void)memset(r, 0, sizeof(*r));
    return PM_OK;
}

pm_err_t pm_relay_on(pm_relay_t *r)
{
    pm_err_t rc;

    if (r == NULL) {
        return PM_ERR_NULL;
    }
    if (r->state != PM_RELAY_STATE_READY) {
        return PM_ERR_STATE;
    }

    rc = relay_drive(r, active_of(r->active_level));
    if (rc == PM_OK) {
        r->on = 1u;
    }
    return rc;
}

pm_err_t pm_relay_off(pm_relay_t *r)
{
    pm_err_t rc;

    if (r == NULL) {
        return PM_ERR_NULL;
    }
    if (r->state != PM_RELAY_STATE_READY) {
        return PM_ERR_STATE;
    }

    rc = relay_drive(r, inactive_of(r->active_level));
    if (rc == PM_OK) {
        r->on = 0u;
    }
    return rc;
}

pm_err_t pm_relay_set(pm_relay_t *r, bool on)
{
    return on ? pm_relay_on(r) : pm_relay_off(r);
}

pm_err_t pm_relay_toggle(pm_relay_t *r)
{
    if (r == NULL) {
        return PM_ERR_NULL;
    }
    if (r->state != PM_RELAY_STATE_READY) {
        return PM_ERR_STATE;
    }
    return pm_relay_set(r, (r->on == 0u));
}

bool pm_relay_is_on(const pm_relay_t *r)
{
    if ((r == NULL) || (r->state != PM_RELAY_STATE_READY)) {
        return false;
    }
    return (r->on != 0u);
}
