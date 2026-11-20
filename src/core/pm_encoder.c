/*
 * pm_encoder.c - quadrature decode and count, hardware independent.
 *
 * Table indexed by [previous AB][new AB]; AB = A << 1 | B.
 * Valid forward edges give +1, valid reverse edges -1, a repeated state and
 * a diagonal jump give 0 (the latter resynchronises without counting).
 */
#include "pm_encoder.h"
#include <string.h>

#define PM_ENCODER_STATE_UNINIT 0u
#define PM_ENCODER_STATE_READY  1u
#define PM_ENCODER_MAGIC        0x454E4331u   /* "ENC1" */

static const int8_t k_step[4][4] = {
    /* columns are AB values in numeric order: 00, 01, 10, 11 */
    /* 00 */ { 0, +1, -1,  0 },
    /* 01 */ {-1,  0,  0, +1 },
    /* 10 */ {+1,  0,  0, -1 },
    /* 11 */ { 0, -1, +1,  0 },
};

static uint8_t swap_ab(uint8_t ab)
{
    return (uint8_t)(((ab & 0x01u) << 1) | ((ab >> 1) & 0x01u));
}

pm_err_t pm_encoder_init(pm_encoder_t *e, const pm_encoder_cfg_t *cfg)
{
    const pm_port_t *port;
    pm_err_t rc;
    uint8_t initial_ab = 0u;

    if ((e == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    if ((e->magic == PM_ENCODER_MAGIC) && (e->state == PM_ENCODER_STATE_READY)) {
        return PM_ERR_BUSY;
    }

    port = pm_port_get();
    if ((port == NULL) || (port->encoder == NULL)) {
        return PM_ERR_STATE;
    }
    if (cfg->hw_id >= port->encoder_count) {
        return PM_ERR_RANGE;
    }
    /* The button is an optional capability: asking for one the port cannot
     * read is a design error, but not asking is the normal case and must not
     * fail (docs/00-contract.md section 19.6). */
    if ((cfg->has_button != 0u) && (port->encoder->read_button == NULL)) {
        return PM_ERR_DISABLED;
    }

    (void)memset(e, 0, sizeof(*e));
    e->hw_id      = cfg->hw_id;
    e->invert     = (cfg->invert != 0u) ? 1u : 0u;
    e->has_button = (cfg->has_button != 0u) ? 1u : 0u;
    e->ops        = port->encoder;
    e->port_ctx   = port->encoder_ctx;

    rc = e->ops->open(e->port_ctx, e, cfg, &initial_ab);
    if (rc != PM_OK) {
        e->ops = NULL;
        return rc;
    }

    /* Adopt the pin state found at open so the first real edge is decoded
     * from the truth, not from an assumed 00. */
    e->last_ab = (uint8_t)(initial_ab & 0x03u);
    e->state   = PM_ENCODER_STATE_READY;
    e->magic   = PM_ENCODER_MAGIC;
    return PM_OK;
}

pm_err_t pm_encoder_deinit(pm_encoder_t *e)
{
    if (e == NULL) {
        return PM_ERR_NULL;
    }
    if (e->state != PM_ENCODER_STATE_READY) {
        return PM_OK;
    }

    e->ops->close(e->port_ctx, e);
    (void)memset(e, 0, sizeof(*e));
    return PM_OK;
}

int32_t pm_encoder_count(const pm_encoder_t *e)
{
    if ((e == NULL) || (e->state != PM_ENCODER_STATE_READY)) {
        return 0;
    }
    return e->count;
}

pm_err_t pm_encoder_reset(pm_encoder_t *e)
{
    if (e == NULL) {
        return PM_ERR_NULL;
    }
    if (e->state != PM_ENCODER_STATE_READY) {
        return PM_ERR_STATE;
    }
    e->count = 0;
    return PM_OK;
}

uint8_t pm_encoder_last_ab(const pm_encoder_t *e)
{
    if ((e == NULL) || (e->state != PM_ENCODER_STATE_READY)) {
        return 0u;
    }
    return e->last_ab;
}

bool pm_encoder_has_button(const pm_encoder_t *e)
{
    if ((e == NULL) || (e->state != PM_ENCODER_STATE_READY)) {
        return false;
    }
    return e->has_button != 0u;
}

pm_err_t pm_encoder_button(pm_encoder_t *e, bool *pressed)
{
    if ((e == NULL) || (pressed == NULL)) {
        return PM_ERR_NULL;
    }
    if (e->state != PM_ENCODER_STATE_READY) {
        return PM_ERR_STATE;
    }
    if (e->has_button == 0u) {
        *pressed = false;
        return PM_ERR_DISABLED;
    }
    return e->ops->read_button(e->port_ctx, e, pressed);
}

void pm_encoder_hw_edge(pm_encoder_t *e, uint8_t ab)
{
    uint8_t now;
    int8_t  step;

    if ((e == NULL) || (e->state != PM_ENCODER_STATE_READY)) {
        return;
    }

    now = (uint8_t)(ab & 0x03u);
    if (e->invert != 0u) {
        now = swap_ab(now);
    }

    step = k_step[e->last_ab & 0x03u][now];
    if (step != 0) {
        e->count += step;
    }
    e->last_ab = now;
}
