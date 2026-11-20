/*
 * pm_servo.h - servo and buzzer outputs used by the KEYSKING learning board.
 *
 * The contract is hardware-free: degrees in 0.1 units, frequency in Hz,
 * duty in percent. Servo is PB8 (TIM4_CH3, 50 Hz); the buzzer is PB9 and is
 * driven by a software phase counter so it can run at its own frequency
 * while the servo keeps its 50 Hz frame: PB9 can be muxed to TIM4_CH4, but
 * this implementation does not use that channel; it toggles the pin from the
 * TIM3 update interrupt. Only the servo occupies a TIM4 compare channel.
 */
#ifndef PM_SERVO_H
#define PM_SERVO_H

#include <stdint.h>

#define PM_PWM_SERVO_MIN_DEG10   0
#define PM_PWM_SERVO_MAX_DEG10   1800

#define PM_PWM_BUZ_MIN_HZ        100u
#define PM_PWM_BUZ_MAX_HZ        5000u
#define PM_PWM_BUZ_DUTY_DEFAULT  20u

/* Returns 0 on success, negative if the timer setup failed. */
int pm_servo_init(void);

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

/* Read-only snapshot for `PWM dbg`. STM32F1 has no MODER/OSPEEDR/AFRH; the
 * PB8 pin mode lives in GPIOB->CRH bits 3:0 and remap state in AFIO->MAPR. */
typedef struct {
    uint32_t rcc_apb1enr;
    uint32_t rcc_apb2enr;
    uint32_t tim4_cr1;
    uint32_t tim4_psc;
    uint32_t tim4_arr;
    uint32_t tim4_ccmr2;
    uint32_t tim4_ccer;
    uint32_t tim4_ccr3;
    uint32_t gpiob_crh;
    uint32_t afio_mapr;
    uint32_t frame_hz10;   /* PWM frame frequency from PSC/ARR, 0.1 Hz */
} pm_pwm_dbg_t;

void     pm_pwm_dbg_read(pm_pwm_dbg_t *out);
uint32_t pm_pwm_counter(void);

/* Temporary PB8 test: begin() detaches TIM4_CH3 and turns PB8 into a plain
 * push-pull GPIO; end() restores alternate function and 90 degrees. */
void pm_pwm_test_begin(void);
void pm_pwm_test_level(int high);
void pm_pwm_test_end(void);

#endif /* PM_SERVO_H */
