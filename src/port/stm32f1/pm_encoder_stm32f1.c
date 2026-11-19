/*
 * pm_encoder_stm32f1.c - PA8/PA9 quadrature decoder, STM32F1 registers only.
 *
 * Both edges of both channels are counted (x4). The transition table rejects
 * bounce and half steps instead of counting them. No timer is allocated.
 */
#include "pm_encoder.h"

#include "stm32f1xx.h"

static volatile int32_t  s_count;
static volatile int32_t  s_step;
static volatile uint32_t s_prev;
static uint32_t          s_ready;

/* +1 / -1 for a valid step, 0 for a bounce or a skipped state.
 * Index is a | (b << 1) with bit0 = A, bit1 = B. */
static const int8_t s_quad[4][4] = {
    { 0,  1, -1,  0 },
    {-1,  0,  0,  1 },
    { 1,  0,  0, -1 },
    { 0, -1,  1,  0 },
};

static uint32_t read_ab(void)
{
    return (GPIOA->IDR >> 8) & 0x3u;
}

void EXTI9_5_IRQHandler(void)
{
    uint32_t pr = EXTI->PR;
    uint32_t cur;
    int32_t  d;

    if ((pr & ((1u << 8) | (1u << 9))) == 0u) {
        return;
    }
    EXTI->PR = (1u << 8) | (1u << 9);

    cur = read_ab();
    d   = (int32_t)s_quad[s_prev][cur];
    if (d != 0) {
        s_count += d;
        s_step   = d;
    }
    s_prev = cur;
}

int pm_encoder_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN
                  | RCC_APB2ENR_AFIOEN;

    /* PA8/PA9 input with pull-up: CRH nibbles 3:0 and 7:4 = 1000. */
    GPIOA->CRH = (GPIOA->CRH & ~0xFFu) | 0x88u;
    GPIOA->BSRR = (1u << 8) | (1u << 9);

    /* PB15 input with pull-up: CRH bits 31:28 = 1000. */
    GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 28)) | (0x8u << 28);
    GPIOB->BSRR = (1u << 15);

    /* EXTI8/9 on port A: EXTICR[2] low byte = 0. */
    AFIO->EXTICR[2] &= ~0xFFu;
    EXTI->IMR  |= (1u << 8) | (1u << 9);
    EXTI->RTSR |= (1u << 8) | (1u << 9);
    EXTI->FTSR |= (1u << 8) | (1u << 9);
    EXTI->PR    = (1u << 8) | (1u << 9);

    s_count = 0;
    s_step  = 0;
    s_prev  = read_ab();

    NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
    NVIC_SetPriority(EXTI9_5_IRQn, 4u);
    NVIC_EnableIRQ(EXTI9_5_IRQn);

    s_ready = 1u;
    return 0;
}

int32_t pm_encoder_count(void)
{
    return s_count;
}

void pm_encoder_reset(void)
{
    /* A single 32-bit store is atomic on Cortex-M3; no critical section. */
    s_count = 0;
}

int32_t pm_encoder_last_step(void)
{
    return s_step;
}

uint32_t pm_encoder_ab(void)
{
    return read_ab();
}

uint32_t pm_encoder_button(void)
{
    return ((GPIOB->IDR >> 15) & 1u);
}
