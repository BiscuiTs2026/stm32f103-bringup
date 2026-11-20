/*
 * pm_motor.c - DRV8833-style direction and stop state machine.
 *
 * Hardware independent: both inputs are ordinary pm_pwm channels, so all
 * register work stays in the port layer.
 */
#include "pm_motor.h"
#include <string.h>

#define PM_MOTOR_STATE_UNINIT 0u
#define PM_MOTOR_STATE_READY  1u
#define PM_MOTOR_MAGIC        0x4D545231u   /* "MTR1" */
#define PM_MOTOR_DUTY_FULL    PM_PWM_DUTY_MAX

/* Coast first, then drive the chosen input, so a direction change never
 * passes through the brake state (both inputs high). */
static pm_err_t motor_run(pm_motor_t *m, uint16_t duty, bool drive_in1)
{
    pm_err_t rc;

    rc = pm_pwm_set_duty(m->in1, 0u);
    if (rc != PM_OK) {
        return rc;
    }
    rc = pm_pwm_set_duty(m->in2, 0u);
    if (rc != PM_OK) {
        return rc;
    }

    return pm_pwm_set_duty(drive_in1 ? m->in1 : m->in2, duty);
}

static pm_err_t motor_coast(pm_motor_t *m)
{
    pm_err_t rc;

    rc = pm_pwm_set_duty(m->in1, 0u);
    if (rc != PM_OK) {
        return rc;
    }
    return pm_pwm_set_duty(m->in2, 0u);
}

static pm_err_t motor_brake(pm_motor_t *m)
{
    pm_err_t rc;

    rc = pm_pwm_set_duty(m->in1, PM_MOTOR_DUTY_FULL);
    if (rc != PM_OK) {
        return rc;
    }
    return pm_pwm_set_duty(m->in2, PM_MOTOR_DUTY_FULL);
}

pm_err_t pm_motor_init(pm_motor_t *m, const pm_motor_cfg_t *cfg)
{
    uint32_t f1;
    uint32_t f2;
    pm_err_t rc;

    if ((m == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    if ((cfg->invert != PM_MOTOR_INVERT_UNSET) &&
        (cfg->invert != PM_MOTOR_INVERT_OFF) &&
        (cfg->invert != PM_MOTOR_INVERT_ON)) {
        return PM_ERR_PARAM;
    }
    if ((cfg->in1 == NULL) || (cfg->in2 == NULL)) {
        return PM_ERR_NULL;
    }
    /* The same channel on both inputs can never produce coast or brake. */
    if (cfg->in1 == cfg->in2) {
        return PM_ERR_PARAM;
    }
    if ((m->magic == PM_MOTOR_MAGIC) && (m->state == PM_MOTOR_STATE_READY)) {
        return PM_ERR_BUSY;
    }

    f1 = pm_pwm_frequency_hz(cfg->in1);
    f2 = pm_pwm_frequency_hz(cfg->in2);
    if ((f1 == 0u) || (f2 == 0u)) {
        return PM_ERR_STATE;      /* a channel is not open */
    }
    if (f1 != f2) {
        /* Same class of bug as the TIM2 collision: one timer, two periods. */
        return PM_ERR_CONFLICT;
    }

    (void)memset(m, 0, sizeof(*m));
    m->in1    = cfg->in1;
    m->in2    = cfg->in2;
    m->invert = (cfg->invert == PM_MOTOR_INVERT_ON) ? 1u : 0u;
    m->dir    = (uint8_t)PM_MOTOR_DIR_FORWARD;

    rc = motor_coast(m);
    if (rc != PM_OK) {
        (void)memset(m, 0, sizeof(*m));
        return rc;
    }

    m->run_state = (uint8_t)PM_MOTOR_STATE_STOP_COAST;
    m->state     = PM_MOTOR_STATE_READY;
    m->magic     = PM_MOTOR_MAGIC;
    return PM_OK;
}

pm_err_t pm_motor_deinit(pm_motor_t *m)
{
    if (m == NULL) {
        return PM_ERR_NULL;
    }
    if (m->state != PM_MOTOR_STATE_READY) {
        return PM_OK;
    }

    /* Fail-safe: never leave the bridge driven. */
    (void)motor_coast(m);
    (void)memset(m, 0, sizeof(*m));
    return PM_OK;
}

pm_err_t pm_motor_set(pm_motor_t *m, uint16_t duty_permille, pm_motor_dir_t dir)
{
    bool drive_in1;
    pm_err_t rc;

    if (m == NULL) {
        return PM_ERR_NULL;
    }
    if (m->state != PM_MOTOR_STATE_READY) {
        return PM_ERR_STATE;
    }
    if (duty_permille > PM_PWM_DUTY_MAX) {
        return PM_ERR_PARAM;
    }
    if ((dir != PM_MOTOR_DIR_FORWARD) && (dir != PM_MOTOR_DIR_REVERSE)) {
        return PM_ERR_PARAM;
    }

    /* Contract: duty 0 IS a coast. Leaving BRAKE is implicit here, because
     * both channels are written to 0 before anything else. */
    if (duty_permille == 0u) {
        rc = motor_coast(m);
        if (rc == PM_OK) {
            m->duty_permille = 0u;
            m->dir           = (uint8_t)dir;
            m->run_state     = (uint8_t)PM_MOTOR_STATE_STOP_COAST;
        }
        return rc;
    }

    drive_in1 = (dir == PM_MOTOR_DIR_FORWARD);
    if (m->invert != 0u) {
        drive_in1 = !drive_in1;
    }

    rc = motor_run(m, duty_permille, drive_in1);
    if (rc == PM_OK) {
        m->duty_permille = duty_permille;
        m->dir           = (uint8_t)dir;
        m->run_state     = (uint8_t)PM_MOTOR_STATE_RUN;
    }
    return rc;
}

pm_err_t pm_motor_stop(pm_motor_t *m, pm_motor_stop_t mode)
{
    pm_err_t rc;

    if (m == NULL) {
        return PM_ERR_NULL;
    }
    if (m->state != PM_MOTOR_STATE_READY) {
        return PM_ERR_STATE;
    }
    if ((mode != PM_MOTOR_STOP_COAST) && (mode != PM_MOTOR_STOP_BRAKE)) {
        return PM_ERR_PARAM;
    }

    if (mode == PM_MOTOR_STOP_BRAKE) {
        rc = motor_brake(m);
        if (rc == PM_OK) {
            m->duty_permille = 0u;
            m->run_state     = (uint8_t)PM_MOTOR_STATE_STOP_BRAKE;
        }
        return rc;
    }

    rc = motor_coast(m);
    if (rc == PM_OK) {
        m->duty_permille = 0u;
        m->run_state     = (uint8_t)PM_MOTOR_STATE_STOP_COAST;
    }
    return rc;
}

uint16_t pm_motor_duty_permille(const pm_motor_t *m)
{
    if ((m == NULL) || (m->state != PM_MOTOR_STATE_READY)) {
        return 0u;
    }
    return m->duty_permille;
}

pm_motor_dir_t pm_motor_dir(const pm_motor_t *m)
{
    if ((m == NULL) || (m->state != PM_MOTOR_STATE_READY)) {
        return PM_MOTOR_DIR_FORWARD;
    }
    return (pm_motor_dir_t)m->dir;
}

pm_motor_state_t pm_motor_state(const pm_motor_t *m)
{
    if ((m == NULL) || (m->state != PM_MOTOR_STATE_READY)) {
        return PM_MOTOR_STATE_STOP_COAST;
    }
    return (pm_motor_state_t)m->run_state;
}

bool pm_motor_is_running(const pm_motor_t *m)
{
    return pm_motor_state(m) == PM_MOTOR_STATE_RUN;
}
