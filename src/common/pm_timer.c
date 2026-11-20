/*
 * pm_timer.c - runtime timer occupancy table, hardware independent.
 *
 * See include/pm/pm_timer.h for the rules and docs/00-contract.md section 20
 * for the failure this prevents.
 */
#include "pm_timer.h"
#include <string.h>

typedef struct {
    uint8_t  used;
    uint8_t  hw_id;
    uint8_t  owner_id;
    uint8_t  role;
    uint16_t psc;
    uint16_t arr;
    uint8_t  channels;
} pm_timer_entry_t;

static pm_timer_entry_t s_claims[PM_TIMER_CLAIM_MAX];
static pm_timer_probe_fn s_probe;
static void             *s_probe_ctx;

void pm_timer_set_probe(pm_timer_probe_fn fn, void *ctx)
{
    s_probe     = fn;
    s_probe_ctx = ctx;
}

static bool claim_valid(const pm_timer_claim_t *c)
{
    if (c->hw_id >= PM_TIMER_ID_MAX) {
        return false;
    }
    switch (c->role) {
    case PM_TIMER_ROLE_PWM:
        return (c->channels != 0u) && ((c->channels & 0xF0u) == 0u);
    case PM_TIMER_ROLE_TONE:
        return c->channels == 0u;
    case PM_TIMER_ROLE_COUNTER:
        return (c->channels == 0u) && (c->arr == PM_TIMER_ARR_FULL);
    default:
        return false;
    }
}

static bool same_config(const pm_timer_entry_t *e, const pm_timer_claim_t *c)
{
    return (e->role == (uint8_t)c->role) && (e->psc == c->psc) &&
           (e->arr == c->arr) && (e->channels == c->channels);
}

/* True when `e` and `c` may share one timer. The caller has already ruled out
 * the same owner. */
static bool compatible(const pm_timer_entry_t *e, const pm_timer_claim_t *c)
{
    if ((e->role != (uint8_t)PM_TIMER_ROLE_PWM) ||
        (c->role != PM_TIMER_ROLE_PWM)) {
        return false;   /* tone/tone, counter/counter, tone/pwm, counter/x */
    }
    if ((e->psc != c->psc) || (e->arr != c->arr)) {
        return false;   /* one time base, one period */
    }
    return (e->channels & c->channels) == 0u;   /* no shared compare channel */
}

static pm_err_t check_locked(const pm_timer_claim_t *claim)
{
    uint8_t i;
    uint8_t free_slot = PM_TIMER_CLAIM_MAX;
    uint8_t live = 0u;
    const pm_timer_entry_t *owner = NULL;

    if (claim == NULL) {
        return PM_ERR_NULL;
    }
    if (!claim_valid(claim)) {
        return PM_ERR_PARAM;
    }

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        const pm_timer_entry_t *e = &s_claims[i];

        if (e->used == 0u) {
            if (free_slot == PM_TIMER_CLAIM_MAX) {
                free_slot = i;
            }
            continue;
        }
        if (e->hw_id != claim->hw_id) {
            continue;
        }
        live++;
        if (e->owner_id == claim->owner_id) {
            owner = e;
            continue;
        }
        if (!compatible(e, claim)) {
            return PM_ERR_CONFLICT;
        }
    }

    /* Nothing in the table knows this timer: ask the port whether somebody
     * outside the model already runs it. Only then, because once our own claim
     * exists the hardware is legitimately busy and the probe would flag us.
     * A busy timer is reported as PM_ERR_BUSY so the caller can tell "started
     * by code we cannot see" apart from "claimed by another module". */
    if ((live == 0u) && (s_probe != NULL) &&
        s_probe(s_probe_ctx, claim->hw_id)) {
        return PM_ERR_BUSY;
    }

    if (owner != NULL) {
        return same_config(owner, claim) ? PM_OK : PM_ERR_CONFLICT;
    }
    return (free_slot < PM_TIMER_CLAIM_MAX) ? PM_OK : PM_ERR_NOMEM;
}

pm_err_t pm_timer_check(const pm_timer_claim_t *claim)
{
    return check_locked(claim);
}

pm_err_t pm_timer_claim(const pm_timer_claim_t *claim)
{
    pm_err_t rc;
    uint8_t i;

    rc = check_locked(claim);
    if (rc != PM_OK) {
        return rc;
    }

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        pm_timer_entry_t *e = &s_claims[i];

        if (e->used == 0u) {
            continue;
        }
        if ((e->hw_id == claim->hw_id) && (e->owner_id == claim->owner_id)) {
            return PM_OK;               /* identical re-claim */
        }
    }

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        pm_timer_entry_t *e = &s_claims[i];

        if (e->used == 0u) {
            e->used     = 1u;
            e->hw_id    = claim->hw_id;
            e->owner_id = claim->owner_id;
            e->role     = (uint8_t)claim->role;
            e->psc      = claim->psc;
            e->arr      = claim->arr;
            e->channels = claim->channels;
            return PM_OK;
        }
    }
    return PM_ERR_NOMEM;                /* unreachable after check */
}

pm_err_t pm_timer_release(uint8_t hw_id, uint8_t owner_id)
{
    uint8_t i;

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        pm_timer_entry_t *e = &s_claims[i];

        if ((e->used != 0u) && (e->hw_id == hw_id) &&
            (e->owner_id == owner_id)) {
            (void)memset(e, 0, sizeof(*e));
            return PM_OK;
        }
    }
    return PM_ERR_STATE;
}

pm_err_t pm_timer_retime(uint8_t hw_id, uint8_t owner_id, uint16_t psc,
                         uint16_t arr)
{
    uint8_t i;
    uint8_t role = 0xFFu;
    bool found = false;

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        const pm_timer_entry_t *e = &s_claims[i];

        if ((e->used == 0u) || (e->hw_id != hw_id)) {
            continue;
        }
        if (e->owner_id == owner_id) {
            role  = e->role;
            found = true;
        }
    }
    if (!found) {
        return PM_ERR_STATE;
    }

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        const pm_timer_entry_t *e = &s_claims[i];

        if ((e->used != 0u) && (e->hw_id == hw_id) && (e->role != role)) {
            return PM_ERR_CONFLICT;    /* cannot retime a foreign time base */
        }
    }

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        pm_timer_entry_t *e = &s_claims[i];

        if ((e->used != 0u) && (e->hw_id == hw_id)) {
            e->psc = psc;
            e->arr = arr;
        }
    }
    return PM_OK;
}

uint8_t pm_timer_claims(uint8_t hw_id)
{
    uint8_t i;
    uint8_t n = 0u;

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        if ((s_claims[i].used != 0u) && (s_claims[i].hw_id == hw_id)) {
            n++;
        }
    }
    return n;
}

pm_err_t pm_timer_claim_at(uint8_t hw_id, uint8_t index, pm_timer_claim_t *out)
{
    uint8_t i;
    uint8_t seen = 0u;

    if (out == NULL) {
        return PM_ERR_NULL;
    }
    if (hw_id >= PM_TIMER_ID_MAX) {
        return PM_ERR_RANGE;
    }

    for (i = 0u; i < PM_TIMER_CLAIM_MAX; i++) {
        const pm_timer_entry_t *e = &s_claims[i];

        if ((e->used == 0u) || (e->hw_id != hw_id)) {
            continue;
        }
        if (seen == index) {
            out->hw_id     = e->hw_id;
            out->owner_id  = e->owner_id;
            out->role      = (pm_timer_role_t)e->role;
            out->psc       = e->psc;
            out->arr       = e->arr;
            out->channels  = e->channels;
            return PM_OK;
        }
        seen++;
    }
    return PM_ERR_RANGE;
}

void pm_timer_reset(void)
{
    (void)memset(s_claims, 0, sizeof(s_claims));
    s_probe     = NULL;
    s_probe_ctx = NULL;
}
