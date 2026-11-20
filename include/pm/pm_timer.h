/*
 * pm_timer.h - runtime arbitration of the hardware timers.
 *
 * Why this exists: on STM32F1 a timer is a single time base (PSC/ARR) shared
 * by every user of that timer. Two modules that configure the same TIM with
 * different PSC/ARR silently overwrite each other; the classic failure is a
 * PWM period landing on top of a 1 us free-running counter used for echo
 * timing. The table below makes that overlap an explicit error instead.
 *
 * Contract (see docs/00-contract.md section 20):
 *  - Every module that owns a timer calls pm_timer_claim() in its init/open
 *    path, declaring hw_id, role, PSC, ARR and the compare channels it uses.
 *  - Claims coexist only when they can really share the time base:
 *      * PWM + PWM: same PSC/ARR and disjoint channels;
 *      * everything else: one owner per timer.
 *  - An incompatible request returns PM_ERR_CONFLICT. Nothing is modified.
 *  - A timer already started by code outside this model returns PM_ERR_BUSY
 *    (the port probe reports it); the model never takes it over.
 *  - Releasing is mandatory in deinit/close, including on the error path.
 *  - The table only sees code that uses this API. Firmware that starts a
 *    timer on its own is invisible, so the port installs a probe (see
 *    pm_timer_set_probe) and a busy timer is refused, never taken over.
 *
 * The table is static, fixed-size and interrupt-safe with respect to
 * single-word writes only; claim/release are meant to run in the main loop.
 */
#ifndef PM_TIMER_H
#define PM_TIMER_H

#include "pm_common.h"
#include "pm_err.h"

#define PM_TIMER_CLAIM_MAX 8u
#define PM_TIMER_ID_MAX    8u
#define PM_TIMER_ARR_FULL  0xFFFFu

typedef enum {
    PM_TIMER_ROLE_PWM = 0,  /* compare output(s), pm_pwm */
    PM_TIMER_ROLE_TONE,     /* update interrupt only, pm_tone */
    PM_TIMER_ROLE_COUNTER   /* free-running counter, ARR = 0xFFFF */
} pm_timer_role_t;

typedef struct {
    uint8_t         hw_id;     /* logical timer instance, board-defined */
    uint8_t         owner_id;  /* chosen by the owner, unique per instance */
    pm_timer_role_t role;
    uint16_t        psc;       /* prescaler register value (divider - 1) */
    uint16_t        arr;       /* auto-reload register value */
    uint8_t         channels;  /* bit 0..3 = channel 1..4, 0 without compare */
} pm_timer_claim_t;

/* Adds a claim. Returns PM_OK, PM_ERR_PARAM, PM_ERR_NULL, PM_ERR_NOMEM when
 * the table is full, or PM_ERR_CONFLICT when the timer already carries an
 * incompatible claim. An identical re-claim by the same owner is a no-op. */
pm_err_t pm_timer_claim(const pm_timer_claim_t *claim);

/* Removes the claim of (hw_id, owner_id). PM_ERR_STATE when not found. */
pm_err_t pm_timer_release(uint8_t hw_id, uint8_t owner_id);

/* Changes PSC/ARR of every claim of the same role on that timer (a PWM
 * frequency change retimes all sibling channels). PM_ERR_CONFLICT when a
 * different role shares the timer, PM_ERR_STATE when the owner has no claim. */
pm_err_t pm_timer_retime(uint8_t hw_id, uint8_t owner_id, uint16_t psc, uint16_t arr);

/* Same rules as pm_timer_claim() but does not modify the table. Useful for
 * a dry run before touching hardware. */
pm_err_t pm_timer_check(const pm_timer_claim_t *claim);

/* External occupancy probe. Returns true when the timer is already started or
 * reconfigured by code outside this model (a hand-written CR1.CEN check, a
 * CubeMX init, a legacy driver). The port installs one; it is a pure read of
 * registers and must not change them.
 *
 * The probe is consulted only while the timer has no live claim. Once the
 * model itself has started the time base, a CEN-based probe would flag our own
 * sibling channels, so it is skipped from then on.
 *
 * A busy probe makes pm_timer_claim() and pm_timer_check() return
 * PM_ERR_BUSY without writing anything. The model never takes a timer over
 * silently (see docs/00-contract.md section 20.7). */
typedef bool (*pm_timer_probe_fn)(void *ctx, uint8_t hw_id);

/* Installs (fn != NULL) or removes (fn == NULL) the probe. Called by the port;
 * the core keeps no knowledge of the hardware. */
void pm_timer_set_probe(pm_timer_probe_fn fn, void *ctx);

/* Number of live claims on a timer (0 = free). */
uint8_t pm_timer_claims(uint8_t hw_id);

/* Reads who holds a timer: copies the index-th live claim of `hw_id`
 * (0-based, in table order) so the caller can see the owner and its config.
 * PM_ERR_RANGE when hw_id is unknown or index >= pm_timer_claims(hw_id). */
pm_err_t pm_timer_claim_at(uint8_t hw_id, uint8_t index, pm_timer_claim_t *out);

/* Clears the whole table and uninstalls the probe (power-on state). Only for
 * test setup and full re-initialisation; the port must re-install its probe
 * afterwards. */
void pm_timer_reset(void);

#endif /* PM_TIMER_H */
