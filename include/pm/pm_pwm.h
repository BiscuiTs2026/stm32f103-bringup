/*
 * pm_pwm.h - PWM output channel model (timer compare output).
 *
 * Lifecycle:  init -> (set_duty / set_frequency / set_pulse_us / enable)*
 *             -> deinit
 *
 * Capabilities:
 *  - One logical channel per object: a timer instance plus one compare
 *    channel. init() configures the pin, the time base and the compare unit,
 *    and starts the output with the configured duty.
 *  - Duty is expressed in permille (0..1000) so no floating point is needed.
 *  - `set_frequency` changes the shared timer period. Channels that live on
 *    the same timer also share that period: if another channel of the same
 *    timer is open, a frequency change that differs from the current one
 *    returns PM_ERR_BUSY instead of silently retiming the sibling. Channels
 *    on the same timer may still have independent duty cycles.
 *  - No interrupt, no event queue, no timeout: PWM output is level-driven by
 *    hardware once configured.
 */
#ifndef PM_PWM_H
#define PM_PWM_H

#include "pm_common.h"
#include "pm_err.h"
#include "pm_port.h"
#include "pm_pwm_math.h"   /* common layer: the PSC/ARR rule the ports call */

#define PM_PWM_DUTY_MAX  1000u

struct pm_pwm {
    uint32_t magic;          /* set by pm_pwm_init, cleared by deinit */
    uint8_t  state;
    uint8_t  hw_id;
    uint8_t  enabled;        /* 1 while the compare output is running */
    uint8_t  reserved;

    uint16_t duty_permille;
    uint32_t freq_hz;        /* effective frequency reported by the port */

    const pm_pwm_ops_t *ops;
    void               *port_ctx;
};

/* Configures the channel and starts the output. The object must be
 * zero-initialized. Returns PM_ERR_PARAM for an out-of-range frequency or
 * duty, PM_ERR_RANGE for an unknown hw_id, PM_ERR_BUSY when the object is
 * already open or the shared timer already runs a different period. */
pm_err_t pm_pwm_init(pm_pwm_t *p, const pm_pwm_cfg_t *cfg);

/* Stops the output and releases the channel. Idempotent. */
pm_err_t pm_pwm_deinit(pm_pwm_t *p);

/* Changes the period. `actual_hz` receives the frequency the timer can
 * actually generate; pass NULL if not needed. */
pm_err_t pm_pwm_set_frequency(pm_pwm_t *p, uint32_t freq_hz, uint32_t *actual_hz);

/* 0 = always low, 1000 = always high (one timer tick of resolution is lost
 * at the top of a 16-bit period). */
pm_err_t pm_pwm_set_duty(pm_pwm_t *p, uint16_t permille);

/* Convenience for servos: pulse width in microseconds at the current
 * frequency, converted to permille. Values longer than the period clamp to
 * 1000 permille. */
pm_err_t pm_pwm_set_pulse_us(pm_pwm_t *p, uint16_t pulse_us);

/* Starts or stops the compare output without changing the configuration. */
pm_err_t pm_pwm_enable(pm_pwm_t *p, bool on);

/* Effective values; 0 for an object that is not initialized. */
uint32_t pm_pwm_frequency_hz(const pm_pwm_t *p);
uint16_t pm_pwm_duty_permille(const pm_pwm_t *p);

#endif /* PM_PWM_H */
