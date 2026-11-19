/*
 * pm_track_stm32f1.c - PB14 line-tracking input, STM32F1 registers only.
 */
#include "pm_track.h"

#include "stm32f1xx.h"

#define TRACK_PIN 14u

int pm_track_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /* PB14 input with pull-up: CRH nibble 27:24 = 1000, ODR=1 selects pull. */
    GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 24)) | (0x8u << 24);
    GPIOB->BSRR = (1u << TRACK_PIN);
    return 0;
}

uint32_t pm_track_raw(void)
{
    return ((GPIOB->IDR >> TRACK_PIN) & 1u);
}
