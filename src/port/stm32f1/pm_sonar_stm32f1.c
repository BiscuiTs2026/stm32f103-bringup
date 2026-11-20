/*
 * pm_sonar_stm32f1.c - HC-SR04 TRIG/ECHO port layer.
 *
 * Moved out of src/app/main.c unchanged in behaviour (split step S6a).
 * HARDWARE.md §3 第 6 条 + 附录引脚表: header 1=GND, 2=PA10 (ECHO),
 * 3=PA11 (TRIG), 4=3.3V. PA10/PA11 are FT (5 V tolerant) and the header feeds
 * the module 3.3 V (HC-SR04P), so ECHO swings 0..3.3 V; no divider is fitted.
 *
 * ECHO is captured by EXTI10 on both edges. 2026-11-20: TIM2 belongs to the
 * motor PWM, so the time stamp moved to the Cortex-M3 DWT cycle counter
 * (DWT->CYCCNT, 72 MHz, wraps every 59.65 s; a measurement is < 25 ms).
 * Decision 0020 records the choice.
 *
 * Interrupt priority is 5: lower than the encoder's EXTI9_5 at 4, so a
 * quadrature edge can preempt the echo handler and no encoder count is lost.
 * This vector is shared only with EXTI11..15, which are unused.
 */
#include "pm_sonar.h"

#include "stm32f1xx.h"

#define SONAR_TRIG_BIT    (1u << 11)
#define SONAR_ECHO_BIT    (1u << 10)
#define SONAR_EXTI_IRQn   EXTI15_10_IRQn
#define SONAR_IRQ_PRIO    5u

static pm_sonar_snapshot_t s_sonar;
static uint32_t             s_cycles_per_us;

/* Microseconds from the wrapped cycle counter. Widths are < 25 ms, so the
 * 16-bit truncation below never sees more than one 59.65 s wrap. */
static uint32_t now_us(void)
{
    return DWT->CYCCNT / s_cycles_per_us;
}

void pm_sonar_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /* PA11 push-pull output 2 MHz, PA10 floating input */
    GPIOA->CRH = (GPIOA->CRH & ~((0xFu << 12) | (0xFu << 8)))
               | (0x2u << 12) | (0x4u << 8);
    GPIOA->BRR = SONAR_TRIG_BIT;

    /* DWT cycle counter as the 1 us time base (TIM2 is the motor PWM). */
    s_cycles_per_us = SystemCoreClock / 1000000u;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    /* EXTI10 <- port A, both edges */
    AFIO->EXTICR[2] = (AFIO->EXTICR[2] & ~(0xFu << 8));
    EXTI->IMR  |= SONAR_ECHO_BIT;
    EXTI->RTSR |= SONAR_ECHO_BIT;
    EXTI->FTSR |= SONAR_ECHO_BIT;
    EXTI->PR    = SONAR_ECHO_BIT;

    NVIC_ClearPendingIRQ(SONAR_EXTI_IRQn);
    NVIC_SetPriority(SONAR_EXTI_IRQn, SONAR_IRQ_PRIO);
    NVIC_EnableIRQ(SONAR_EXTI_IRQn);
}

void pm_sonar_pulse(void)
{
    uint32_t t0 = DWT->CYCCNT;
    uint32_t span = 10u * s_cycles_per_us;

    /* 10 us TRIG pulse, timed by the cycle counter so it does not depend on
     * loop speed. */
    GPIOA->BSRR = SONAR_TRIG_BIT;
    while ((uint32_t)(DWT->CYCCNT - t0) < span) {
    }
    GPIOA->BRR = SONAR_TRIG_BIT;
}

void pm_sonar_clear_edge(void)
{
    EXTI->PR = SONAR_ECHO_BIT;             /* write 1 to clear */
    s_sonar.rise_us   = 0u;
    s_sonar.width_us  = 0u;
    s_sonar.have_rise = 0u;
    s_sonar.done      = 0u;
}

void pm_sonar_snapshot(pm_sonar_snapshot_t *out)
{
    /* done first: once it reads 1, width_us is stable until the next pulse. */
    out->done      = s_sonar.done;
    out->width_us  = s_sonar.width_us;
    out->rise_us   = s_sonar.rise_us;
    out->have_rise = s_sonar.have_rise;
}

void EXTI15_10_IRQHandler(void)
{
    uint32_t t;

    if ((EXTI->PR & SONAR_ECHO_BIT) == 0u) {
        return;
    }
    EXTI->PR = SONAR_ECHO_BIT;             /* write 1 to clear */

    t = now_us();
    if ((GPIOA->IDR & SONAR_ECHO_BIT) != 0u) {
        s_sonar.rise_us   = (uint16_t)t;
        s_sonar.have_rise = 1u;
    } else if (s_sonar.have_rise != 0u) {
        s_sonar.width_us  = (uint16_t)(t - s_sonar.rise_us);
        s_sonar.have_rise = 0u;
        s_sonar.done      = 1u;
    }
}
