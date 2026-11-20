/*
 * pm_pwm_math.c - pure time base math, hardware independent.
 *
 * Common layer on purpose: the PWM model and the port both call it, so it
 * cannot belong to either side (see include/pm/pm_pwm_math.h).
 */
#include "pm_pwm_math.h"

static bool freq_valid(uint32_t hz)
{
    return (hz >= PM_PWM_FREQ_MIN) && (hz <= PM_PWM_FREQ_MAX);
}

pm_err_t pm_pwm_timing_compute(uint32_t timer_clk_hz, uint32_t freq_hz,
                               pm_pwm_timing_t *out)
{
    uint32_t p;
    uint32_t a;
    uint32_t actual;
    uint32_t diff;

    if (out == NULL) {
        return PM_ERR_NULL;
    }
    if ((timer_clk_hz == 0u) || !freq_valid(freq_hz)) {
        return PM_ERR_PARAM;
    }

    /* PSC first: the whole period must fit the 16-bit ARR. PSC = 0 with a
     * 50 Hz request is exactly the "72 MHz used as 1 MHz, ARR capped at
     * 19999, output 3.6 kHz" trap seen on the board. */
    p = (timer_clk_hz / freq_hz) / 65536u;
    if (p > 65535u) {
        p = 65535u;
    }
    a = (timer_clk_hz / (p + 1u)) / freq_hz;
    if (a == 0u) {
        a = 1u;
    }
    if (a > 65536u) {
        a = 65536u;
    }

    out->psc = (uint16_t)p;
    out->arr = (uint16_t)(a - 1u);

    actual = timer_clk_hz / ((p + 1u) * a);
    if (actual == 0u) {
        actual = 1u;
    }
    out->actual_hz = actual;

    /* Mandatory self-check: after the registers are written, the frequency
     * must still be the one that was asked for. Refuse instead of starting
     * an output that silently runs at another time base. */
    diff = (actual > freq_hz) ? (actual - freq_hz) : (freq_hz - actual);
    if ((uint64_t)diff * 1000u > (uint64_t)freq_hz * 10u) {
        return PM_ERR_PARAM;
    }
    return PM_OK;
}

bool pm_pwm_timing_matches(uint32_t timer_clk_hz, uint16_t psc, uint16_t arr,
                           uint32_t freq_hz, uint16_t tolerance_permille)
{
    uint64_t divisor;
    uint64_t scaled_clk;
    uint64_t scaled_req;
    uint64_t diff;
    uint64_t allowed;

    if ((timer_clk_hz == 0u) || !freq_valid(freq_hz)) {
        return false;
    }

    /* Cross-multiply instead of rounding the actual frequency first: with
     * floor(timer_clk / divisor) a legal pair can fall just outside its own
     * tolerance window (449.9 Hz floored to 449 against a 450 Hz request),
     * and the board implementation compares the same way. */
    divisor    = ((uint64_t)psc + 1u) * ((uint64_t)arr + 1u);
    scaled_clk = (uint64_t)timer_clk_hz * 1000u;
    scaled_req = (uint64_t)freq_hz * 1000u * divisor;
    diff       = (scaled_clk > scaled_req) ? (scaled_clk - scaled_req)
                                           : (scaled_req - scaled_clk);
    allowed    = (uint64_t)freq_hz * tolerance_permille * divisor;
    return diff <= allowed;
}

uint16_t pm_pwm_compare_count(uint16_t arr, uint16_t permille)
{
    uint32_t period = (uint32_t)arr + 1u;
    uint32_t compare = (period * permille) / 1000u;

    if (compare > period) {
        compare = period;
    }
    if (compare > 0xFFFFu) {
        compare = 0xFFFFu;    /* one tick lost at 100% on a 65536-tick period */
    }
    return (uint16_t)compare;
}
