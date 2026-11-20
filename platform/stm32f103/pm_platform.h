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

#endif /* PM_PLATFORM_H */
