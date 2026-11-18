/*
 * pm_osal_port_cm3.c - OSAL implementation for Cortex-M3 (STM32F1).
 *
 * This is the only OSAL file allowed to include a CMSIS device header.
 * Everything above it uses pm_osal.h and never sees PRIMASK / SysTick.
 */
#include "pm_osal.h"
#include "stm32f1xx.h"

static volatile uint32_t s_ms;

pm_err_t pm_osal_init(void)
{
    s_ms = 0u;
    return PM_OK;
}

uint32_t pm_osal_now_ms(void)
{
    return s_ms;
}

void pm_osal_delay_ms(uint32_t ms)
{
    uint32_t start = s_ms;

    while ((uint32_t)(s_ms - start) < ms) {
        __WFI();
    }
}

pm_irq_state_t pm_osal_critical_enter(void)
{
    pm_irq_state_t state = (pm_irq_state_t)__get_PRIMASK();
    __disable_irq();
    __DMB();
    return state;
}

void pm_osal_critical_exit(pm_irq_state_t state)
{
    __DMB();
    __set_PRIMASK(state);
}

void pm_osal_tick_inc(void)
{
    s_ms++;
}
