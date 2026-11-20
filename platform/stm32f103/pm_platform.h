/*
 * pm_platform.h - minimal platform bring-up for the STM32F103 example.
 */
#ifndef PM_PLATFORM_H
#define PM_PLATFORM_H

#include "pm_common.h"

/* Powers up the tick source (SysTick 1 ms) and programs the clock tree
 * (HSE 8 MHz -> PLL 72 MHz, falling back to HSI 8 MHz). Call once, before
 * pm_osal_init(). */
void pm_platform_init(void);

/* System clock in Hz, valid after pm_platform_init(). */
uint32_t pm_platform_core_clock(void);

/* Board diagnostic text behind "MOTOR dbg" (temporary, 2026-11-20): TIM2 /
 * GPIOA register snapshot plus three CNT samples, so "timer not running" and
 * "pins were reconfigured" can be told apart without a probe. Returns one
 * CRLF-terminated line; the buffer is static and valid until the next call.
 * Bound into the motor component by the entry layer (main.c). */
const char *pm_platform_motor_dbg(void);

/* Debug-only fault injector behind "MOTOR force timer|remap" (2026-11-20).
 * mode 0 starts TIM2 outside the model, mode 1 sets the AFIO TIM2 remap field;
 * both run the motor channel's claim/classification path and then restore the
 * registers and the claim table. Returns 0 restored, -1 precondition,
 * -2 restore failed. Do not rely on this in production firmware. */
int pm_platform_motor_force(uint8_t mode, int *rc, uint32_t *bits,
                            uint8_t *before, uint8_t *after);

#endif /* PM_PLATFORM_H */
