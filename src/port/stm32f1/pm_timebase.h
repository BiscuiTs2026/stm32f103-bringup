/*
 * pm_timebase.h - integer-only check that a timer's (PSC, ARR) pair really
 * produces the frequency the caller means.
 *
 * 2026-11-20 contract alignment (decision 0018): the board keeps its verified
 * servo and motor time bases instead of the model's computed ones, but every
 * write must be re-checked against the actual register values first. This is
 * the board-side equivalent of pm_pwm_timing_matches(). No HAL here on
 * purpose: the same function can be compiled and run on the host.
 */
#ifndef PM_TIMEBASE_H
#define PM_TIMEBASE_H

#include <stdint.h>

/* Cross-multiplied on purpose: rounding the actual frequency to an integer
 * first would make a valid pair fail its own tolerance at the boundary. */
static inline int pm_timebase_ok(uint32_t clk_hz, uint32_t psc, uint32_t arr,
                                 uint32_t hz, uint32_t tol_permille)
{
    uint64_t den  = ((uint64_t)psc + 1u) * ((uint64_t)arr + 1u);
    uint64_t want = (uint64_t)hz * den;
    uint64_t diff = (want > (uint64_t)clk_hz) ? (want - (uint64_t)clk_hz)
                                              : ((uint64_t)clk_hz - want);

    return ((diff * 1000u) <= ((uint64_t)hz * (uint64_t)tol_permille * den)) ? 1 : 0;
}

#endif /* PM_TIMEBASE_H */
