/*
 * pm_timer_stm32f1.c - register-only occupancy probe for TIM2/TIM3/TIM4.
 *
 * Same two facts the motor init used inline since the TIM2 conflict
 * (docs/TIM2-CONFLICT.md): CR1.CEN means someone already started the counter,
 * a non-zero AFIO remap field means the channel pins are not where the caller
 * assumes. Read-only: this file writes nothing.
 */
#include "pm_timer.h"

#include "stm32f1xx.h"

uint32_t pm_timer_probe(pm_timer_hw_t hw_id)
{
    uint32_t busy = PM_TIMER_FREE;

    switch (hw_id) {
    case PM_TIMER_T2:
        if ((TIM2->CR1 & TIM_CR1_CEN) != 0u) {
            busy |= PM_TIMER_BUSY_CEN;
        }
        /* Both remap bits: partial remap (10) moves CH1/CH2 as well, and the
         * old single-bit check silently missed that case. */
        if ((AFIO->MAPR & AFIO_MAPR_TIM2_REMAP) != 0u) {
            busy |= PM_TIMER_BUSY_REMAP;
        }
        break;
    case PM_TIMER_T3:
        if ((TIM3->CR1 & TIM_CR1_CEN) != 0u) {
            busy |= PM_TIMER_BUSY_CEN;
        }
        if ((AFIO->MAPR & AFIO_MAPR_TIM3_REMAP) != 0u) {
            busy |= PM_TIMER_BUSY_REMAP;
        }
        break;
    case PM_TIMER_T4:
        if ((TIM4->CR1 & TIM_CR1_CEN) != 0u) {
            busy |= PM_TIMER_BUSY_CEN;
        }
        if ((AFIO->MAPR & AFIO_MAPR_TIM4_REMAP) != 0u) {
            busy |= PM_TIMER_BUSY_REMAP;
        }
        break;
    default:
        busy = PM_TIMER_BUSY_UNKNOWN;
        break;
    }
    return busy;
}
