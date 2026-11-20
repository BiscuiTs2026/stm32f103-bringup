/*
 * pm_pwm_tim2_stm32f1.c - PWM port ops for PA0/PA1 = TIM2_CH1/CH2.
 *
 * TIM2 runs at 72 MHz (APB1 36 MHz x2) and both compare channels share one
 * period, which is exactly the constraint the pm_pwm model documents: the
 * second channel may only open with the same frequency, otherwise open()
 * returns PM_ERR_BUSY. The two channels drive the DRV8833 inputs through the
 * pm_motor model; only one of them carries a duty at a time and the other
 * stays at 0, which is the DRV8833 "drive" combination. Both low is coast.
 *
 * TIM2 was the HC-SR04 time base until 2026-11-19 (DIST is disabled now), so
 * the occupancy probe still decides whether someone else owns it.
 */
#include "pm_pwm_tim2_stm32f1.h"

#include "stm32f1xx.h"

#include "pm_motor_dbg.h"
#include "pm_pwm.h"
#include "pm_pwm_math.h"
#include "pm_timer_stm32f1.h"

#define TIM2_CLK_HZ     72000000u
#define TIM2_CH_COUNT   2u
#define TIM2_CCMR1_PWM  ((0x6u << 4) | (0x1u << 3) | (0x6u << 12) | (0x1u << 11))

typedef struct {
    uint8_t  opened;        /* bit set once a channel went through open() */
    uint16_t permille[2];   /* last duty per channel, replayed on retime */
    uint16_t psc;
    uint16_t arr;
    uint32_t freq_hz;
} tim2_state_t;

static tim2_state_t s_t2;

static uint8_t ch_of(const pm_pwm_t *p)
{
    return p->hw_id;
}

static uint8_t owner_of(uint8_t ch)
{
    return (ch == 0u) ? PM_F1_TIMER_OWNER_TIM2_CH1
                      : PM_F1_TIMER_OWNER_TIM2_CH2;
}

static void set_ccr(uint8_t ch, uint16_t value)
{
    if (ch == 0u) {
        TIM2->CCR1 = value;
    } else {
        TIM2->CCR2 = value;
    }
}

static uint32_t ccer_bit(uint8_t ch)
{
    return (ch == 0u) ? TIM_CCER_CC1E : TIM_CCER_CC2E;
}

static pm_err_t tim2_claim_channel(uint8_t ch, uint16_t psc, uint16_t arr,
                                   uint8_t owner, uint32_t *busy_bits)
{
    pm_timer_claim_t claim;
    pm_err_t rc;

    claim.hw_id    = PM_F1_TIMER_TIM2;
    claim.owner_id = owner;
    claim.role     = PM_TIMER_ROLE_PWM;
    claim.psc      = psc;
    claim.arr      = arr;
    claim.channels = (uint8_t)(1u << ch);

    rc = pm_timer_claim(&claim);
    if (rc != PM_ERR_BUSY) {
        return rc;
    }

    /* CEN set  -> someone else is counting          -> err=timer
     * remap set-> pins are not where the caller says -> err=remap
     * PM_ERR_STATE is the component-side carrier for the remap case. */
    {
        uint32_t busy = pm_f1_timer_busy_bits(PM_F1_TIMER_TIM2);

        if (busy_bits != NULL) {
            *busy_bits = busy;
        }
        if ((busy & PM_F1_TIMER_BUSY_CEN) != 0u) {
            return PM_ERR_BUSY;
        }
        if ((busy & PM_F1_TIMER_BUSY_REMAP) != 0u) {
            return PM_ERR_STATE;
        }
        return PM_ERR_BUSY;
    }
}

static pm_err_t tim2_open(void *ctx, pm_pwm_t *p, const pm_pwm_cfg_t *cfg,
                          uint32_t *actual_hz)
{
    pm_pwm_timing_t t;
    pm_err_t rc;
    uint8_t ch;

    (void)ctx;
    if ((p == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    if (ch_of(p) >= TIM2_CH_COUNT) {
        return PM_ERR_RANGE;
    }

    rc = pm_pwm_timing_compute(TIM2_CLK_HZ, cfg->freq_hz, &t);
    if (rc != PM_OK) {
        return rc;
    }

    ch = ch_of(p);
    if ((s_t2.opened & (uint8_t)(1u << ch)) != 0u) {
        return PM_ERR_BUSY;
    }

    /* Arbitration is the model's job (pm_timer.c v0.8.0): a sibling with a
     * different PSC/ARR and a timer started outside the model are both refused
     * before any register write. Only the error refinement stays local, so the
     * two visible motor errors keep their old split. */
    rc = tim2_claim_channel(ch, t.psc, t.arr, owner_of(ch), NULL);
    if (rc != PM_OK) {
        return rc;              /* PM_ERR_CONFLICT: sibling config differs */
    }

    if (s_t2.opened == 0u) {
        RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
        RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

        /* PA0 (CRL 3:0) and PA1 (CRL 7:4): alternate-function push-pull
         * 50 MHz. Both pins are configured when the timer is taken over. */
        GPIOA->BRR = (1u << 0) | (1u << 1);
        GPIOA->CRL = (GPIOA->CRL & ~((0xFu << 0) | (0xFu << 4)))
                   | (0xBu << 0) | (0xBu << 4);

        TIM2->CR1   = 0u;
        TIM2->PSC   = t.psc;
        TIM2->ARR   = t.arr;
        TIM2->CCR1  = 0u;
        TIM2->CCR2  = 0u;
        /* OC1/OC2 PWM mode 1 with preload so a duty write takes effect on the
         * next update event instead of tearing the current pulse. */
        TIM2->CCMR1 = TIM2_CCMR1_PWM;
        TIM2->CCER  = TIM_CCER_CC1E | TIM_CCER_CC2E;
        TIM2->EGR   = TIM_EGR_UG;
        TIM2->SR    = 0u;
        TIM2->CR1   = TIM_CR1_ARPE | TIM_CR1_CEN;

        s_t2.psc     = t.psc;
        s_t2.arr     = t.arr;
        s_t2.freq_hz = t.actual_hz;
    }

    s_t2.permille[ch] = cfg->duty_permille;
    set_ccr(ch, pm_pwm_compare_count(t.arr, cfg->duty_permille));
    s_t2.opened |= (uint8_t)(1u << ch);

    if (actual_hz != NULL) {
        *actual_hz = s_t2.freq_hz;
    }
    return PM_OK;
}

static void tim2_close(void *ctx, pm_pwm_t *p)
{
    uint8_t ch;

    (void)ctx;
    if ((p == NULL) || (ch_of(p) >= TIM2_CH_COUNT)) {
        return;
    }
    ch = ch_of(p);
    if ((s_t2.opened & (uint8_t)(1u << ch)) == 0u) {
        return;
    }

    s_t2.opened &= (uint8_t)~(uint8_t)(1u << ch);
    s_t2.permille[ch] = 0u;
    set_ccr(ch, 0u);
    TIM2->CCER &= ~ccer_bit(ch);
    (void)pm_timer_release(PM_F1_TIMER_TIM2, owner_of(ch));

    if (s_t2.opened == 0u) {
        TIM2->CR1 &= ~TIM_CR1_CEN;
        s_t2.freq_hz = 0u;
    }
}

static pm_err_t tim2_set_freq(void *ctx, pm_pwm_t *p, uint32_t freq_hz,
                              uint32_t *actual_hz)
{
    pm_pwm_timing_t t;
    pm_err_t rc;
    uint8_t ch;
    uint8_t i;

    (void)ctx;
    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (ch_of(p) >= TIM2_CH_COUNT) {
        return PM_ERR_RANGE;
    }
    if (s_t2.opened == 0u) {
        return PM_ERR_STATE;
    }

    rc = pm_pwm_timing_compute(TIM2_CLK_HZ, freq_hz, &t);
    if (rc != PM_OK) {
        return rc;
    }

    ch = ch_of(p);
    if ((s_t2.opened & (uint8_t)~(uint8_t)(1u << ch)) != 0u) {
        /* A sibling is open: the model allows sharing only at the same
         * PSC/ARR, so a different period is an internal conflict. */
        if ((t.psc != s_t2.psc) || (t.arr != s_t2.arr)) {
            return PM_ERR_CONFLICT;
        }
        if (actual_hz != NULL) {
            *actual_hz = s_t2.freq_hz;
        }
        return PM_OK;
    }

    if ((t.psc == s_t2.psc) && (t.arr == s_t2.arr)) {
        if (actual_hz != NULL) {
            *actual_hz = s_t2.freq_hz;
        }
        return PM_OK;
    }

    rc = pm_timer_retime(PM_F1_TIMER_TIM2, owner_of(ch), t.psc, t.arr);
    if (rc != PM_OK) {
        return rc;
    }

    TIM2->CR1 &= ~TIM_CR1_CEN;
    TIM2->PSC  = t.psc;
    TIM2->ARR  = t.arr;
    TIM2->EGR  = TIM_EGR_UG;
    s_t2.psc     = t.psc;
    s_t2.arr     = t.arr;
    s_t2.freq_hz = t.actual_hz;

    /* Re-apply every open channel's duty against the new period. */
    for (i = 0u; i < TIM2_CH_COUNT; i++) {
        if ((s_t2.opened & (uint8_t)(1u << i)) != 0u) {
            set_ccr(i, pm_pwm_compare_count(s_t2.arr, s_t2.permille[i]));
        }
    }
    TIM2->SR  = 0u;
    TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    if (actual_hz != NULL) {
        *actual_hz = s_t2.freq_hz;
    }
    return PM_OK;
}

static pm_err_t tim2_set_duty(void *ctx, pm_pwm_t *p, uint16_t permille)
{
    uint8_t ch;

    (void)ctx;
    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (ch_of(p) >= TIM2_CH_COUNT) {
        return PM_ERR_RANGE;
    }
    ch = ch_of(p);
    if ((s_t2.opened & (uint8_t)(1u << ch)) == 0u) {
        return PM_ERR_STATE;
    }
    if (permille > PM_PWM_DUTY_MAX) {
        return PM_ERR_PARAM;
    }

    s_t2.permille[ch] = permille;
    set_ccr(ch, pm_pwm_compare_count(s_t2.arr, permille));
    return PM_OK;
}

static pm_err_t tim2_enable(void *ctx, pm_pwm_t *p, bool on)
{
    uint8_t ch;

    (void)ctx;
    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (ch_of(p) >= TIM2_CH_COUNT) {
        return PM_ERR_RANGE;
    }
    ch = ch_of(p);
    if ((s_t2.opened & (uint8_t)(1u << ch)) == 0u) {
        return PM_ERR_STATE;
    }

    if (on) {
        TIM2->CCER |= ccer_bit(ch);
    } else {
        TIM2->CCER &= ~ccer_bit(ch);
    }
    return PM_OK;
}

/* Debug-only seam for "MOTOR force timer|remap" (2026-11-20). It withholds
 * the motor's two claims for the duration of one probe, injects the requested
 * fault exactly the way an outside owner would, runs the same
 * tim2_claim_channel() call tim2_open() uses, then puts the registers and the
 * two claims back. mode 0: set CR1.CEN; mode 1: set the AFIO TIM2_REMAP field.
 * Returns 0 when the claim table is back to what it was, -1 precondition,
 * -2 restore failed. *rc receives the pm_err_t tim2_open() would have returned. */
#define TIM2_REMAP_MASK 0x300u

int pm_f1_tim2_force_fault(uint8_t mode, int *rc, uint32_t *bits,
                           uint8_t *before, uint8_t *after)
{
    uint32_t cr1;
    uint32_t ccer;
    uint32_t cnt;
    uint32_t mapr;
    uint32_t seen = 0u;
    uint8_t  n_before;
    uint8_t  n_after;
    uint8_t  i;
    pm_err_t result;

    if ((mode > 1u) || (s_t2.opened != 0x03u)) {
        return -1;
    }
    n_before = pm_timer_claims(PM_F1_TIMER_TIM2);
    if (n_before != 2u) {
        return -1;
    }
    for (i = 0u; i < 2u; i++) {
        pm_timer_claim_t c;

        if ((pm_timer_claim_at(PM_F1_TIMER_TIM2, i, &c) != PM_OK) ||
            (c.owner_id != owner_of(i)) || (c.role != PM_TIMER_ROLE_PWM)) {
            return -1;
        }
    }

    cr1  = TIM2->CR1;
    ccer = TIM2->CCER;
    cnt  = TIM2->CNT;
    mapr = AFIO->MAPR;

    /* Outputs off and counter stopped: the injected fault must not reach the
     * DRV8833 inputs or shift the PWM phase. */
    TIM2->CCER = 0u;
    TIM2->CR1  = cr1 & ~TIM_CR1_CEN;
    TIM2->CNT  = 0u;

    (void)pm_timer_release(PM_F1_TIMER_TIM2, owner_of(0u));
    (void)pm_timer_release(PM_F1_TIMER_TIM2, owner_of(1u));

    if (mode == 0u) {
        TIM2->CR1 = (cr1 & ~TIM_CR1_CEN) | TIM_CR1_CEN;      /* external start */
    } else {
        AFIO->MAPR = (mapr & ~TIM2_REMAP_MASK) | (0x1u << 8);
    }

    result = tim2_claim_channel(0u, s_t2.psc, s_t2.arr,
                                PM_F1_TIMER_OWNER_DIAG, &seen);

    /* Undo the fault first, then re-claim while the probe still reads clear. */
    TIM2->CR1  = cr1 & ~TIM_CR1_CEN;
    TIM2->CNT  = 0u;
    AFIO->MAPR = mapr;
    for (i = 0u; i < 2u; i++) {
        if ((s_t2.opened & (uint8_t)(1u << i)) != 0u) {
            (void)tim2_claim_channel(i, s_t2.psc, s_t2.arr, owner_of(i), NULL);
        }
    }
    n_after = pm_timer_claims(PM_F1_TIMER_TIM2);

    TIM2->CNT  = cnt;
    TIM2->CCER = ccer;
    TIM2->CR1  = cr1;

    if (rc != NULL) {
        *rc = (int)result;
    }
    if (bits != NULL) {
        *bits = seen;
    }
    if (before != NULL) {
        *before = n_before;
    }
    if (after != NULL) {
        *after = n_after;
    }
    return (n_after == n_before) ? 0 : -2;
}

const pm_pwm_ops_t pm_f1_tim2_pwm_ops = {
    .open     = tim2_open,
    .close    = tim2_close,
    .set_freq = tim2_set_freq,
    .set_duty = tim2_set_duty,
    .enable   = tim2_enable,
};

void pm_motor_hw_snapshot(pm_motor_hw_t *out)
{
    uint32_t div;

    if (out == NULL) {
        return;
    }
    out->arr = TIM2->ARR;
    div      = ((uint32_t)TIM2->PSC + 1u) * (out->arr + 1u);
    out->freq_hz = (div == 0u) ? 0u : (TIM2_CLK_HZ / div);
    out->ccr1    = TIM2->CCR1;
    out->ccr2    = TIM2->CCR2;
}
