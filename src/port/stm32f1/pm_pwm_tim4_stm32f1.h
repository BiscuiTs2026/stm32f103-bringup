/*
 * pm_pwm_tim4_stm32f1.h - private seam between the port registry and the
 * TIM4_CH3 PWM backend used by the servo.
 */
#ifndef PM_PWM_TIM4_STM32F1_H
#define PM_PWM_TIM4_STM32F1_H

#include "pm_port.h"
#include "pm_servo.h"   /* PM_PWM_HW_SERVO */

extern const pm_pwm_ops_t pm_f1_tim4_pwm_ops;

#endif /* PM_PWM_TIM4_STM32F1_H */
