/*
 * pm_timer_stm32f1.c - register-only occupancy probe for TIM2/TIM3/TIM4.
 *
 * The claim table itself lives in src/common/pm_timer.c (model v0.8.0); this
 * file only answers the model's probe callback. Two register facts, unchanged
 * since the TIM2 conflict (docs/TIM2-CONFLICT.md): CR1.CEN means someone
 * already started the counter, a non-zero AFIO remap field means the channel
 * pins are not where the caller assumes. Read-only: writes nothing.
 */
#include "pm_timer_stm32f1.h"

#include "stm32f1xx.h"

#include "pm_timer.h"

uint32_t pm_f1_timer_busy_bits(uint8_t hw_id)
{
    uint32_t busy = 0u;

    switch (hw_id) {
    case PM_F1_TIMER_TIM2:
        if ((TIM2->CR1 & TIM_CR1_CEN) != 0u) {
            busy |= PM_F1_TIMER_BUSY_CEN;
        }
        /* Both remap bits: partial remap (10) moves CH1/CH2 as well, and the
         * old single-bit check silently missed that case. */
        if ((AFIO->MAPR & AFIO_MAPR_TIM2_REMAP) != 0u) {
            busy |= PM_F1_TIMER_BUSY_REMAP;
        }
        break;
    case PM_F1_TIMER_TIM3:
        if ((TIM3->CR1 & TIM_CR1_CEN) != 0u) {
            busy |= PM_F1_TIMER_BUSY_CEN;
        }
        if ((AFIO->MAPR & AFIO_MAPR_TIM3_REMAP) != 0u) {
            busy |= PM_F1_TIMER_BUSY_REMAP;
        }
        break;
    case PM_F1_TIMER_TIM4:
        if ((TIM4->CR1 & TIM_CR1_CEN) != 0u) {
            busy |= PM_F1_TIMER_BUSY_CEN;
        }
        if ((AFIO->MAPR & AFIO_MAPR_TIM4_REMAP) != 0u) {
            busy |= PM_F1_TIMER_BUSY_REMAP;
        }
        break;
    default:
        /* Unknown timer: report busy so the model refuses to take it over. */
        busy = PM_F1_TIMER_BUSY_CEN;
        break;
    }
    return busy;
}

static bool f1_timer_probe(void *ctx, uint8_t hw_id)
{
    (void)ctx;
    return pm_f1_timer_busy_bits(hw_id) != 0u;
}

void pm_f1_timer_probe_install(void)
{
    pm_timer_set_probe(f1_timer_probe, NULL);
}
