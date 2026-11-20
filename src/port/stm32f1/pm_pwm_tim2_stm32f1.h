/*
 * pm_pwm_tim2_stm32f1.h - private seam between the port registry and the
 * TIM2 CH1/CH2 PWM backend used by the motor model.
 */
#ifndef PM_PWM_TIM2_STM32F1_H
#define PM_PWM_TIM2_STM32F1_H

#include "pm_port.h"

#define PM_F1_PWM_TIM2_COUNT 2u   /* CH1 and CH2 share one timer */

extern const pm_pwm_ops_t pm_f1_tim2_pwm_ops;

/* Debug-only, do not rely on this in production firmware. "MOTOR force
 * timer|remap" uses it to trigger the two init failure branches for real:
 * withholds the motor's claims, starts TIM2 externally (mode 0) or sets the
 * AFIO TIM2 remap field (mode 1), runs the same claim/classification path as
 * open(), then restores registers and claims. *rc receives the pm_err_t that
 * open() would have returned (PM_ERR_BUSY -> err=timer, PM_ERR_STATE ->
 * err=remap). Returns 0 when the table is restored, -1 precondition,
 * -2 restore failed. */
int pm_f1_tim2_force_fault(uint8_t mode, int *rc, uint32_t *bits,
                           uint8_t *before, uint8_t *after);

#endif /* PM_PWM_TIM2_STM32F1_H */
