/*
 * pm_osal.h - the only time / concurrency primitives the upper layers may use.
 *
 * Contract:
 *  - No upper layer may call SysTick, NVIC, or RTOS APIs directly.
 *  - The tick is a free-running 32-bit millisecond counter. It wraps every
 *    ~49.7 days; all comparisons must use unsigned subtraction
 *    (now - start), never "now > start".
 *  - Critical sections are nestable and must be balanced. The returned state
 *    is opaque to callers.
 *  - None of these functions may be called from an ISR except
 *    pm_osal_tick_inc(), which is ISR-only.
 */
#ifndef PM_OSAL_H
#define PM_OSAL_H

#include "pm_common.h"
#include "pm_err.h"

/* Opaque saved interrupt state. */
typedef uint32_t pm_irq_state_t;

/* Must be called once after the platform clock and tick are running. */
pm_err_t pm_osal_init(void);

/* Milliseconds since pm_osal_init(). ISR-safe. */
uint32_t pm_osal_now_ms(void);

/* Busy-wait delay. Not allowed in an ISR. */
void pm_osal_delay_ms(uint32_t ms);

/* Nestable critical section. Not allowed in an ISR. */
pm_irq_state_t pm_osal_critical_enter(void);
void           pm_osal_critical_exit(pm_irq_state_t state);

/* Called by the platform tick ISR exactly once per millisecond. */
void pm_osal_tick_inc(void);

#endif /* PM_OSAL_H */
