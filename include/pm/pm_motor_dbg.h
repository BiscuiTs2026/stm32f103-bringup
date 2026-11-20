/*
 * pm_motor_dbg.h - read-only timer snapshot for the MOTOR command.
 *
 * The motor model owns duty and direction, but `MOTOR stat` also reports the
 * timer facts (hz/arr/ccr1/ccr2) so a reply proves what the hardware really
 * holds. The component layer may not touch registers, so the port exposes
 * this one read-only snapshot next to the model contract.
 */
#ifndef PM_MOTOR_DBG_H
#define PM_MOTOR_DBG_H

#include <stdint.h>

typedef struct {
    uint32_t freq_hz;
    uint32_t arr;
    uint32_t ccr1;
    uint32_t ccr2;
} pm_motor_hw_t;

/* Reads the TIM2 registers. Values are zero before the timer is configured. */
void pm_motor_hw_snapshot(pm_motor_hw_t *out);

#endif /* PM_MOTOR_DBG_H */
