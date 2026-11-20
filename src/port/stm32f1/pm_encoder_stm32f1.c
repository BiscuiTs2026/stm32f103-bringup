/*
 * pm_encoder_stm32f1.c - PA8/PA9 quadrature port, STM32F1 registers only.
 *
 * The decode table and the count live in the model (src/core/pm_encoder.c).
 * This file only owns the board side: pin setup, EXTI8/9 both edges, the
 * optional PB15 button, and forwarding each edge state to pm_encoder_hw_edge().
 *
 * 2026-11-20 (decision 0018 §6): this file is only in the build when CMake
 * option PM_ENABLE_ENCODER is ON, which is the default. Turning it off removes
 * the file, so the port ops and EXTI9_5_IRQHandler are not defined and nothing
 * arms EXTI8/9 or the NVIC line; the startup file's weak default takes the
 * vector instead. Priority 4 below is the verified value and is deliberately
 * kept: sonar gave up its line, do not pull it back.
 */
#include "pm_encoder.h"

#include "stm32f1xx.h"

#define ENC_EXTI_MASK ((1u << 8) | (1u << 9))

static pm_encoder_t *s_owner;
static uint8_t       s_opened;

/* Port-facing AB value: bit1 = A (PA8), bit0 = B (PA9), the model's layout. */
static uint8_t read_ab(void)
{
    uint32_t idr = GPIOA->IDR;
    uint8_t  a   = (uint8_t)((idr >> 8) & 1u);
    uint8_t  b   = (uint8_t)((idr >> 9) & 1u);

    return (uint8_t)((a << 1) | b);
}

void EXTI9_5_IRQHandler(void)
{
    if ((EXTI->PR & ENC_EXTI_MASK) == 0u) {
        return;
    }
    EXTI->PR = ENC_EXTI_MASK;

    /* The model keeps its own previous state, so every edge is forwarded. */
    if (s_owner != NULL) {
        pm_encoder_hw_edge(s_owner, read_ab());
    }
}

static pm_err_t f1_encoder_open(void *ctx, pm_encoder_t *e,
                                const pm_encoder_cfg_t *cfg,
                                uint8_t *initial_ab)
{
    PM_UNUSED(ctx);

    if ((e == NULL) || (cfg == NULL) || (initial_ab == NULL)) {
        return PM_ERR_NULL;
    }
    if (cfg->hw_id != 0u) {
        return PM_ERR_RANGE;
    }
    if (s_opened != 0u) {
        return PM_ERR_BUSY;
    }

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN
                  | RCC_APB2ENR_AFIOEN;

    /* PA8/PA9 input with pull-up: CRH nibbles 3:0 and 7:4 = 1000. */
    GPIOA->CRH = (GPIOA->CRH & ~0xFFu) | 0x88u;
    GPIOA->BSRR = (1u << 8) | (1u << 9);

    /* PB15 input with pull-up: CRH bits 31:28 = 1000. */
    if (cfg->has_button != 0u) {
        GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 28)) | (0x8u << 28);
        GPIOB->BSRR = (1u << 15);
    }

    /* EXTI8/9 on port A: EXTICR[2] low byte = 0. */
    AFIO->EXTICR[2] &= ~0xFFu;
    EXTI->IMR  |= ENC_EXTI_MASK;
    EXTI->RTSR |= ENC_EXTI_MASK;
    EXTI->FTSR |= ENC_EXTI_MASK;
    EXTI->PR    = ENC_EXTI_MASK;

    /* Adopt the resting level before the first interrupt can land. */
    *initial_ab = read_ab();
    s_owner     = e;
    s_opened    = 1u;

    NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
    NVIC_SetPriority(EXTI9_5_IRQn, 4u);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    return PM_OK;
}

static void f1_encoder_close(void *ctx, pm_encoder_t *e)
{
    PM_UNUSED(ctx);
    PM_UNUSED(e);

    EXTI->IMR  &= ~ENC_EXTI_MASK;
    EXTI->RTSR &= ~ENC_EXTI_MASK;
    EXTI->FTSR &= ~ENC_EXTI_MASK;
    EXTI->PR    = ENC_EXTI_MASK;
    NVIC_DisableIRQ(EXTI9_5_IRQn);

    s_owner  = NULL;
    s_opened = 0u;
}

static pm_err_t f1_encoder_read_button(void *ctx, pm_encoder_t *e,
                                       bool *pressed)
{
    PM_UNUSED(ctx);
    PM_UNUSED(e);

    if (pressed == NULL) {
        return PM_ERR_NULL;
    }
    if (s_opened == 0u) {
        return PM_ERR_STATE;
    }

    /* KY-040 switch closes to GND; the pull-up defines "released". */
    *pressed = ((GPIOB->IDR & (1u << 15)) == 0u);
    return PM_OK;
}

const pm_encoder_ops_t pm_f1_encoder_ops = {
    .open        = f1_encoder_open,
    .close       = f1_encoder_close,
    .read_button = f1_encoder_read_button,
};
