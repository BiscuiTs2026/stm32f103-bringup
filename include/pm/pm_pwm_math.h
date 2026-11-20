/*
 * pm_pwm_math.h - pure time base math shared by the PWM model and its ports.
 *
 * Why this is a separate header and a separate layer: these are the only
 * functions that know the PSC/ARR rule, and the port (bsp) must call them
 * while it writes the timer registers. Keeping them in the component header
 * would make bsp depend on components, which is the reverse of the layer
 * direction. They are hardware independent (integer math on plain values),
 * so they live in the common layer: any layer may include this header.
 *
 * Contract: docs/00-contract.md section 14.5 (mandatory self-check) and
 * section 20 (timer arbitration).
 */
#ifndef PM_PWM_MATH_H
#define PM_PWM_MATH_H

#include "pm_common.h"
#include "pm_err.h"

#define PM_PWM_FREQ_MIN 1u
#define PM_PWM_FREQ_MAX 1000000u

typedef struct {
    uint16_t psc;       /* prescaler register value (divider - 1) */
    uint16_t arr;       /* auto-reload register value */
    uint32_t actual_hz; /* frequency those two registers really produce */
} pm_pwm_timing_t;

/* Chooses PSC first so the 16-bit ARR can hold the whole period, then ARR.
 * Returns PM_ERR_PARAM when the timer clock cannot reproduce freq_hz within
 * 1% (a request the hardware could only fake). This is the one place the
 * PSC/ARR rule lives; every port must use it instead of dividing itself. */
pm_err_t pm_pwm_timing_compute(uint32_t timer_clk_hz, uint32_t freq_hz,
                               pm_pwm_timing_t *out);

/* Self-check for timing that was not produced by pm_pwm_timing_compute()
 * (hand-written registers, a formerly verified board config, a bug fix).
 * Returns true when the PSC/ARR pair produces freq_hz within
 * `tolerance_permille` thousandths. Cross-multiplies instead of rounding the
 * actual frequency first, so a legal pair is not rejected at its own
 * tolerance edge. Example: 72 MHz, PSC 0, ARR 19999 is 3.6 kHz, so it does
 * NOT match 50 Hz. */
bool pm_pwm_timing_matches(uint32_t timer_clk_hz, uint16_t psc, uint16_t arr,
                           uint32_t freq_hz, uint16_t tolerance_permille);

/* CCR value for a duty in permille. `arr` is the register value, so the
 * period is arr + 1 ticks. 1000 permille clamps to 0xFFFF (one tick is lost
 * on a 65536-tick period). */
uint16_t pm_pwm_compare_count(uint16_t arr, uint16_t permille);

#endif /* PM_PWM_MATH_H */
