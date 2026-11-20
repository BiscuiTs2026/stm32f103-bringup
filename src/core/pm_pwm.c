/*
 * pm_pwm.c - PWM channel model, independent of any vendor header.
 *
 * The model validates the request, keeps the effective values and forwards
 * every change to the port. There is no interrupt and no event queue: after
 * configuration the hardware drives the pin on its own.
 */
#include "pm_pwm.h"
#include <string.h>

#define PM_PWM_STATE_UNINIT 0u
#define PM_PWM_STATE_READY  1u

#define PM_PWM_MAGIC        0x50574D31u   /* "PWM1" */

static bool freq_valid(uint32_t hz)
{
    return (hz >= PM_PWM_FREQ_MIN) && (hz <= PM_PWM_FREQ_MAX);
}

pm_err_t pm_pwm_init(pm_pwm_t *p, const pm_pwm_cfg_t *cfg)
{
    const pm_port_t *port;
    pm_err_t rc;
    uint32_t actual_hz = 0u;

    if ((p == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    if (!freq_valid(cfg->freq_hz) || (cfg->duty_permille > PM_PWM_DUTY_MAX)) {
        return PM_ERR_PARAM;
    }
    if ((p->magic == PM_PWM_MAGIC) && (p->state == PM_PWM_STATE_READY)) {
        return PM_ERR_BUSY;
    }

    port = pm_port_get();
    if ((port == NULL) || (port->pwm == NULL)) {
        return PM_ERR_STATE;
    }
    if (cfg->hw_id >= port->pwm_count) {
        return PM_ERR_RANGE;
    }

    (void)memset(p, 0, sizeof(*p));
    p->hw_id    = cfg->hw_id;
    p->ops      = port->pwm;
    p->port_ctx = port->pwm_ctx;

    rc = p->ops->open(p->port_ctx, p, cfg, &actual_hz);
    if (rc != PM_OK) {
        p->ops = NULL;
        return rc;
    }

    p->freq_hz       = (actual_hz != 0u) ? actual_hz : cfg->freq_hz;
    p->duty_permille = cfg->duty_permille;
    p->enabled       = 1u;            /* open() leaves the output running */
    p->state         = PM_PWM_STATE_READY;
    p->magic         = PM_PWM_MAGIC;
    return PM_OK;
}

pm_err_t pm_pwm_deinit(pm_pwm_t *p)
{
    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (p->state != PM_PWM_STATE_READY) {
        return PM_OK;                 /* already closed */
    }

    p->ops->close(p->port_ctx, p);
    p->state         = PM_PWM_STATE_UNINIT;
    p->magic         = 0u;
    p->ops           = NULL;
    p->enabled       = 0u;
    p->freq_hz       = 0u;
    p->duty_permille = 0u;
    return PM_OK;
}

pm_err_t pm_pwm_set_frequency(pm_pwm_t *p, uint32_t freq_hz, uint32_t *actual_hz)
{
    pm_err_t rc;
    uint32_t effective = 0u;

    if (actual_hz != NULL) {
        *actual_hz = 0u;
    }
    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (p->state != PM_PWM_STATE_READY) {
        return PM_ERR_STATE;
    }
    if (!freq_valid(freq_hz)) {
        return PM_ERR_PARAM;
    }

    rc = p->ops->set_freq(p->port_ctx, p, freq_hz, &effective);
    if (rc != PM_OK) {
        return rc;
    }

    p->freq_hz = (effective != 0u) ? effective : freq_hz;
    if (actual_hz != NULL) {
        *actual_hz = p->freq_hz;
    }
    return PM_OK;
}

pm_err_t pm_pwm_set_duty(pm_pwm_t *p, uint16_t permille)
{
    pm_err_t rc;

    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (p->state != PM_PWM_STATE_READY) {
        return PM_ERR_STATE;
    }
    if (permille > PM_PWM_DUTY_MAX) {
        return PM_ERR_PARAM;
    }

    rc = p->ops->set_duty(p->port_ctx, p, permille);
    if (rc == PM_OK) {
        p->duty_permille = permille;
    }
    return rc;
}

pm_err_t pm_pwm_set_pulse_us(pm_pwm_t *p, uint16_t pulse_us)
{
    uint32_t permille;
    uint32_t product;

    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (p->state != PM_PWM_STATE_READY) {
        return PM_ERR_STATE;
    }

    /* permille = pulse_us / period_us * 1000 = pulse_us * freq_hz / 1000.
     * The split below keeps the product inside 32 bits for freq <= 1 MHz. */
    product = ((uint32_t)pulse_us * (p->freq_hz / 1000u)) +
              (((uint32_t)pulse_us * (p->freq_hz % 1000u)) / 1000u);
    permille = product;
    if (permille > PM_PWM_DUTY_MAX) {
        permille = PM_PWM_DUTY_MAX;
    }
    return pm_pwm_set_duty(p, (uint16_t)permille);
}

pm_err_t pm_pwm_enable(pm_pwm_t *p, bool on)
{
    pm_err_t rc;

    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (p->state != PM_PWM_STATE_READY) {
        return PM_ERR_STATE;
    }

    rc = p->ops->enable(p->port_ctx, p, on);
    if (rc == PM_OK) {
        p->enabled = on ? 1u : 0u;
    }
    return rc;
}

uint32_t pm_pwm_frequency_hz(const pm_pwm_t *p)
{
    if ((p == NULL) || (p->state != PM_PWM_STATE_READY)) {
        return 0u;
    }
    return p->freq_hz;
}

uint16_t pm_pwm_duty_permille(const pm_pwm_t *p)
{
    if ((p == NULL) || (p->state != PM_PWM_STATE_READY)) {
        return 0u;
    }
    return p->duty_permille;
}

