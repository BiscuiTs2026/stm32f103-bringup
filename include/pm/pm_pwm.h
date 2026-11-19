/*
 * pm_pwm.h - PWM outputs used by the KEYSKING learning board.
 *
 * The contract is hardware-free: degrees in 0.1 units, frequency in Hz,
 * duty in percent. Servo is PB8 (TIM4_CH3, 50 Hz); the buzzer is PB9 and is
 * driven by a software phase counter so it can run at its own frequency
 * while the servo keeps its 50 Hz frame (both pins share TIM4).
 */
#ifndef PM_PWM_H
#define PM_PWM_H

#include <stdint.h>

#define PM_PWM_SERVO_MIN_DEG10   0
#define PM_PWM_SERVO_MAX_DEG10   1800

#define PM_PWM_BUZ_MIN_HZ        100u
#define PM_PWM_BUZ_MAX_HZ        5000u
#define PM_PWM_BUZ_DUTY_DEFAULT  20u

/* Returns 0 on success, negative if the timer setup failed. */
int pm_pwm_init(void);

/* deg10: 0.1 degree, 0..1800. Returns 0 or -1 when out of range. */
int pm_pwm_servo_set(int32_t deg10);

/* Pulse width the servo output is currently programmed with, in microseconds. */
uint32_t pm_pwm_servo_pulse_us(void);
int32_t  pm_pwm_servo_deg10(void);

/* hz: 100..5000, duty: 1..99 percent. Returns 0 or -1 when out of range. */
int pm_pwm_buzzer_set(uint32_t hz, uint8_t duty_pct);
void pm_pwm_buzzer_off(void);

/* Effective values after integer rounding of the phase counter. */
uint32_t pm_pwm_buzzer_hz(void);
uint8_t  pm_pwm_buzzer_duty(void);
int      pm_pwm_buzzer_on(void);

#endif /* PM_PWM_H */
