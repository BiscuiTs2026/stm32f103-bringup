/*
 * pm_timer.h - read-only probe for hardware timer occupancy.
 *
 * Decision 0018 (item 5): the board only provides the probe step for now.
 * There is no claim/release owner table, no role or channel arbitration and
 * no PM_ERR_CONFLICT path yet; those stay in a later, separate round.
 *
 * The criteria are the two register facts already used by the motor init:
 * the timer is counting (CR1.CEN) or its pins were remapped away in
 * AFIO->MAPR. Nothing is written and no module state is remembered here.
 */
#ifndef PM_TIMER_H
#define PM_TIMER_H

#include <stdint.h>

/* hw_id values used by this board; the number is the timer index so a dump
 * or a log line reads the same as the reference manual. */
typedef enum {
    PM_TIMER_T2 = 2,
    PM_TIMER_T3 = 3,
    PM_TIMER_T4 = 4
} pm_timer_hw_t;

/* Bits returned by pm_timer_probe(); 0 means "looks free". */
#define PM_TIMER_FREE     0u
#define PM_TIMER_BUSY_CEN   1u   /* CR1.CEN already set */
#define PM_TIMER_BUSY_REMAP 2u   /* AFIO->MAPR remap field non-zero */
#define PM_TIMER_BUSY_UNKNOWN 4u /* unknown hw_id: caller must not take over */

/* Read-only. Never writes a register and never blocks. */
uint32_t pm_timer_probe(pm_timer_hw_t hw_id);

#endif /* PM_TIMER_H */
