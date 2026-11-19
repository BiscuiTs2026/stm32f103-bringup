/*
 * pm_relay_stm32f1.c - PB5 relay driver, STM32F1 registers only.
 *
 * PB5 sits in GPIOB->CRL (pin 5 uses bits 23:20). Output push-pull 2 MHz is
 * nibble value 0x2; the pin starts low so the coil stays released at boot.
 */
#include "pm_relay.h"

#include "stm32f1xx.h"

#define RELAY_PIN 5u

int pm_relay_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /* PB5 output push-pull 2 MHz, then drive low before the mode takes over. */
    GPIOB->BRR  = (1u << RELAY_PIN);
    GPIOB->CRL  = (GPIOB->CRL & ~(0xFu << 20)) | (0x2u << 20);
    return 0;
}

void pm_relay_set(int on)
{
    if (on != 0) {
        GPIOB->BSRR = (1u << RELAY_PIN);
    } else {
        GPIOB->BRR = (1u << RELAY_PIN);
    }
}

uint32_t pm_relay_get(void)
{
    return ((GPIOB->ODR >> RELAY_PIN) & 1u);
}
