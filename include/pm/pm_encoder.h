/*
 * pm_encoder.h - quadrature encoder input on the KEYSKING learning board.
 *
 * A = PA8, B = PA9, push button = PB15, all active-low logic with the internal
 * pull-ups enabled. Decoding happens in the EXTI9_5 interrupt with a x4
 * transition table, so no timer is used and the servo/buzzer timers (TIM3,
 * TIM4) are untouched.
 */
#ifndef PM_ENCODER_H
#define PM_ENCODER_H

#include <stdint.h>

/* PA8/PA9/PB15 as inputs with pull-up, EXTI8/9 rising and falling.
 * Returns 0 on success. */
int pm_encoder_init(void);

/* Signed x4 transition count since init or the last reset. */
int32_t pm_encoder_count(void);

/* Clears the count. The last decoded step is kept. */
void pm_encoder_reset(void);

/* Direction of the last decoded transition: -1, 0 (none yet) or +1. */
int32_t pm_encoder_last_step(void);

/* Raw levels, bit0 = A (PA8), bit1 = B (PA9). */
uint32_t pm_encoder_ab(void);

/* 1 = released (pull-up high), 0 = pressed. */
uint32_t pm_encoder_button(void);

#endif /* PM_ENCODER_H */
