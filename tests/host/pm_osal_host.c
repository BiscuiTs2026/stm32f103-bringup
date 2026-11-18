/*
 * pm_osal_host.c - OSAL implementation for host unit tests.
 *
 * Time only moves when the test advances it, so timeout paths are
 * deterministic and fast.
 */
#include "pm_osal.h"

static uint32_t s_ms;

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
    s_ms += ms;
}

pm_irq_state_t pm_osal_critical_enter(void)
{
    return 0u;                         /* single-threaded host: nothing to mask */
}

void pm_osal_critical_exit(pm_irq_state_t state)
{
    (void)state;
}

void pm_osal_tick_inc(void)
{
    s_ms++;
}

/* Test-only helpers. */
void host_time_advance(uint32_t ms)
{
    s_ms += ms;
}

void host_time_reset(void)
{
    s_ms = 0u;
}
