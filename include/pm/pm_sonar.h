/*
 * pm_sonar.h - HC-SR04 echo capture contract (PA11 TRIG, PA10 ECHO).
 *
 * The implementation lives in src/port/stm32f1/pm_sonar_stm32f1.c; that bsp
 * file is the only place allowed to touch GPIOA/AFIO/EXTI/TIM2 registers and
 * the EXTI15_10 interrupt. Callers only see integer time stamps and never a
 * register.
 */
#ifndef PM_SONAR_H
#define PM_SONAR_H

#include <stdint.h>

/* Written by the EXTI10 handler, read by the measurement state machine.
 * Times are microseconds from DWT->CYCCNT (72 MHz), already truncated to
 * 16 bits: one echo is < 25 ms, so the 65.5 ms truncation window is safe. */
typedef struct {
    uint16_t          rise_us;
    uint16_t          width_us;
    volatile uint8_t  have_rise;
    volatile uint8_t  done;
} pm_sonar_snapshot_t;

/* GPIOA/AFIO/EXTI/NVIC setup plus the DWT cycle counter used as the 1 us
 * time base. Does not configure TIM2: the counter belongs to the motor PWM
 * (docs/TIM2-CONFLICT.md).
 *
 * Interrupt priority: EXTI15_10 is set to 5, numerically lower priority than
 * the encoder's EXTI9_5 at 4 (ARM: larger number = lower priority). A
 * quadrature edge can therefore preempt the echo handler; decision 0020
 * fixes both values. */
void pm_sonar_init(void);

/* One 10 us TRIG pulse, timed by DWT->CYCCNT so it does not depend on loop
 * speed. Requires pm_sonar_init() to have enabled the cycle counter. */
void pm_sonar_pulse(void);

/* Clears the pending EXTI10 flag and the edge snapshot. */
void pm_sonar_clear_edge(void);

/* Copies the current edge snapshot out. */
void pm_sonar_snapshot(pm_sonar_snapshot_t *out);

#endif /* PM_SONAR_H */
