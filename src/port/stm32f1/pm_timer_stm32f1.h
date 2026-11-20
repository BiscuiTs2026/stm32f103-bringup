/*
 * pm_timer_stm32f1.h - board side of the model's timer claim table.
 *
 * The model (include/pm/pm_timer.h, verbatim from v0.8.0) owns the arbitration
 * rules; this header only names the hardware and the owner ids used by this
 * board, and exposes the read-only probe the model asks for.
 */
#ifndef PM_TIMER_STM32F1_H
#define PM_TIMER_STM32F1_H

#include <stdint.h>

#include "pm_timer.h"

/* Logical timer ids. The number is the peripheral index so a claim dump reads
 * the same as the reference manual. Must stay below PM_TIMER_ID_MAX (8). */
#define PM_F1_TIMER_TIM2  2u
#define PM_F1_TIMER_TIM3  3u
#define PM_F1_TIMER_TIM4  4u

/* Owner ids: unique per claim entry. One per PWM channel on TIM2 and one for
 * the TIM4 servo frame; the buzzer keeps TIM3 outside the table for now. */
#define PM_F1_TIMER_OWNER_TIM2_CH1  0x21u
#define PM_F1_TIMER_OWNER_TIM2_CH2  0x22u
#define PM_F1_TIMER_OWNER_TIM4      0x24u
/* Never used by the firmware; only "MOTOR force timer|remap" borrows it. */
#define PM_F1_TIMER_OWNER_DIAG      0x7Eu

/* Probe result bits, same meaning as the pre-claim probe (decision 0018):
 * CEN already set = someone started the counter; a non-zero AFIO remap field
 * = the pins are not where the caller assumes. */
#define PM_F1_TIMER_BUSY_CEN    1u
#define PM_F1_TIMER_BUSY_REMAP  2u

/* Installs the model probe (pm_timer_set_probe). Called once from
 * pm_pwm_board_init() before any claim is made. */
void pm_f1_timer_probe_install(void);

/* Read-only register snapshot used by the probe and by the MOTOR diagnostics
 * that must keep err=timer and err=remap apart. Never writes. Unknown hw_id
 * returns PM_F1_TIMER_BUSY_CEN so nobody takes an unknown timer over. */
uint32_t pm_f1_timer_busy_bits(uint8_t hw_id);

#endif /* PM_TIMER_STM32F1_H */
