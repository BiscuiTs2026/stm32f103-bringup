/*
 * pm_sonar_stm32f1.c - HC-SR04 TRIG/ECHO port layer.
 *
 * Moved out of src/app/main.c unchanged in behaviour (split step S6a).
 * HARDWARE.md §3 第 6 条 + 附录引脚表: header 1=GND, 2=PA10 (ECHO),
 * 3=PA11 (TRIG), 4=3.3V. PA10/PA11 are FT (5 V tolerant) and the header feeds
 * the module 3.3 V (HC-SR04P), so ECHO swings 0..3.3 V; no divider is fitted.
 *
 * ECHO is captured by EXTI10 on both edges and timestamped by TIM2 at 1 MHz.
 * 2026-11-19: TIM2 was handed to the motor PWM, so this file does not
 * configure the counter; DIST stays disabled in the caller
 * (DIST_TIME_BASE_READY 0). Pins and edge logic are kept as-is so a future
 * time source can re-enable them.
 *
 * 2026-11-20: pm_sonar_init() is no longer called from the entry layer while
 * DIST is disabled. Its EXTI/NVIC arming below must be revisited before it is
 * called again: it enables EXTI15_10_IRQn without setting a priority, so the
 * IRQ would run at the reset priority 0 (higher than UART 1, TIM3 3 and the
 * encoder's EXTI9_5 at 4). No other peripheral shares that vector today.
 */
#include "pm_sonar.h"

#include "stm32f1xx.h"

#define SONAR_TRIG_BIT  (1u << 11)
#define SONAR_ECHO_BIT  (1u << 10)

static pm_sonar_snapshot_t s_sonar;

void pm_sonar_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /* PA11 push-pull output 2 MHz, PA10 floating input */
    GPIOA->CRH = (GPIOA->CRH & ~((0xFu << 12) | (0xFu << 8)))
               | (0x2u << 12) | (0x4u << 8);
    GPIOA->BRR = SONAR_TRIG_BIT;

    /* TIM2 is not configured here: it belongs to the motor PWM. */

    /* EXTI10 <- port A, both edges */
    AFIO->EXTICR[2] = (AFIO->EXTICR[2] & ~(0xFu << 8));
    EXTI->IMR  |= SONAR_ECHO_BIT;
    EXTI->RTSR |= SONAR_ECHO_BIT;
    EXTI->FTSR |= SONAR_ECHO_BIT;
    EXTI->PR    = SONAR_ECHO_BIT;
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void pm_sonar_pulse(void)
{
    uint16_t t0 = (uint16_t)TIM2->CNT;

    /* 10 us TRIG pulse, timed by TIM2 so it does not depend on loop speed. */
    GPIOA->BSRR = SONAR_TRIG_BIT;
    while ((uint16_t)((uint16_t)TIM2->CNT - t0) < 10u) {
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
    if ((EXTI->PR & SONAR_ECHO_BIT) == 0u) {
        return;
    }
    EXTI->PR = SONAR_ECHO_BIT;             /* write 1 to clear */

    if ((GPIOA->IDR & SONAR_ECHO_BIT) != 0u) {
        s_sonar.rise_us   = (uint16_t)TIM2->CNT;
        s_sonar.have_rise = 1u;
    } else if (s_sonar.have_rise != 0u) {
        s_sonar.width_us  = (uint16_t)((uint16_t)TIM2->CNT - s_sonar.rise_us);
        s_sonar.have_rise = 0u;
        s_sonar.done      = 1u;
    }
}
