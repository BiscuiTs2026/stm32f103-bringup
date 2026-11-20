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
#include "pm_timer.h"

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
    if (s_t2.opened != 0u) {
        /* One timer, one period: a sibling with another PSC/ARR is exactly
         * the conflict the model refuses to hide. */
        if ((t.psc != s_t2.psc) || (t.arr != s_t2.arr)) {
            return PM_ERR_BUSY;
        }
        if ((s_t2.opened & (uint8_t)(1u << ch)) != 0u) {
            return PM_ERR_BUSY;
        }
    } else {
        uint32_t busy = pm_timer_probe(PM_TIMER_T2);

        if (busy != PM_TIMER_FREE) {
            return ((busy & PM_TIMER_BUSY_CEN) != 0u) ? PM_ERR_BUSY
                                                      : PM_ERR_STATE;
        }

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
        /* A sibling is open: the period is shared, so only a no-op is safe. */
        if ((t.psc != s_t2.psc) || (t.arr != s_t2.arr)) {
            return PM_ERR_BUSY;
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
