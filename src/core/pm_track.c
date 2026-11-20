/*
 * pm_track.c - line-tracking sensor model, hardware independent.
 */
#include "pm_track.h"
#include <string.h>

#define PM_TRACK_STATE_UNINIT 0u
#define PM_TRACK_STATE_READY  1u
#define PM_TRACK_MAGIC        0x54524B31u   /* "TRK1" */

pm_err_t pm_track_init(pm_track_t *t, const pm_track_cfg_t *cfg)
{
    const pm_port_t *port;
    pm_err_t rc;

    if ((t == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    if ((cfg->active_level != (uint8_t)PM_GPIO_LEVEL_LOW) &&
        (cfg->active_level != (uint8_t)PM_GPIO_LEVEL_HIGH)) {
        return PM_ERR_PARAM;
    }
    if ((t->magic == PM_TRACK_MAGIC) && (t->state == PM_TRACK_STATE_READY)) {
        return PM_ERR_BUSY;
    }

    port = pm_port_get();
    if ((port == NULL) || (port->track == NULL)) {
        return PM_ERR_STATE;
    }
    if (cfg->hw_id >= port->track_count) {
        return PM_ERR_RANGE;
    }

    (void)memset(t, 0, sizeof(*t));
    t->hw_id        = cfg->hw_id;
    t->active_level = cfg->active_level;
    t->ops          = port->track;
    t->port_ctx     = port->track_ctx;

    rc = t->ops->open(t->port_ctx, t, cfg);
    if (rc != PM_OK) {
        t->ops = NULL;
        return rc;
    }

    t->state = PM_TRACK_STATE_READY;
    t->magic = PM_TRACK_MAGIC;
    return PM_OK;
}

pm_err_t pm_track_deinit(pm_track_t *t)
{
    if (t == NULL) {
        return PM_ERR_NULL;
    }
    if (t->state != PM_TRACK_STATE_READY) {
        return PM_OK;
    }

    t->ops->close(t->port_ctx, t);
    (void)memset(t, 0, sizeof(*t));
    return PM_OK;
}

pm_err_t pm_track_read_raw(pm_track_t *t, bool *on_line)
{
    bool high = false;
    pm_err_t rc;

    if ((t == NULL) || (on_line == NULL)) {
        return PM_ERR_NULL;
    }
    if (t->state != PM_TRACK_STATE_READY) {
        return PM_ERR_STATE;
    }

    rc = t->ops->read(t->port_ctx, t, &high);
    if (rc != PM_OK) {
        return rc;
    }

    *on_line = high ? (t->active_level == (uint8_t)PM_GPIO_LEVEL_HIGH)
                    : (t->active_level == (uint8_t)PM_GPIO_LEVEL_LOW);
    return PM_OK;
}
