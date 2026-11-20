/*
 * pm_pwm_tim4_stm32f1.c - PWM port ops for PB8 = TIM4_CH3 (servo frame).
 *
 * The register sequence is the one that used to live in pm_pwm_stm32f1.c:
 * 1 MHz tick, 20 ms frame, PWM mode 1 with preload on OC3. Only the time base
 * source of the numbers changed - PSC/ARR now come from the shared
 * pm_pwm_timing_compute() rule and are re-checked before any register write,
 * so the 3.6 kHz prescaler accident (PATCHES 32) stays impossible.
 *
 * TIM4 is not shared with the buzzer: PB9 is driven by TIM3 update interrupts,
 * so this backend owns the whole timer and never has a sibling channel.
 */
#include "pm_pwm_tim4_stm32f1.h"

#include "stm32f1xx.h"

#include "pm_pwm.h"
#include "pm_pwm_math.h"
#include "pm_timer_stm32f1.h"

#define TIM4_CLK_HZ        72000000u  /* APB1 36 MHz x2 for timers */
#define TIM4_TOL_PERMILLE  10u        /* decision 0018: 10 permille */

typedef struct {
    uint8_t  opened;
    uint16_t permille;
    uint16_t psc;
    uint16_t arr;
    uint32_t freq_hz;
} tim4_state_t;

static tim4_state_t s_t4;

static pm_err_t tim4_open(void *ctx, pm_pwm_t *p, const pm_pwm_cfg_t *cfg,
                          uint32_t *actual_hz)
{
    pm_pwm_timing_t t;
    pm_timer_claim_t claim;
    pm_err_t rc;

    (void)ctx;
    if ((p == NULL) || (cfg == NULL)) {
        return PM_ERR_NULL;
    }
    if (p->hw_id != PM_PWM_HW_SERVO) {
        return PM_ERR_RANGE;
    }
    if (s_t4.opened != 0u) {
        return PM_ERR_BUSY;
    }

    rc = pm_pwm_timing_compute(TIM4_CLK_HZ, cfg->freq_hz, &t);
    if (rc != PM_OK) {
        return rc;
    }
    /* Same guard the hand-written driver had: never touch the registers for a
     * pair that does not produce the requested frame rate. */
    if (!pm_pwm_timing_matches(TIM4_CLK_HZ, t.psc, t.arr, cfg->freq_hz,
                               TIM4_TOL_PERMILLE)) {
        return PM_ERR_PARAM;
    }

    /* TIM4 is in the claim table too: a second user of the same timer gets
     * PM_ERR_CONFLICT (different PSC/ARR or role) or PM_ERR_BUSY (started
     * outside the model) instead of silently overwriting this time base. */
    claim.hw_id    = PM_F1_TIMER_TIM4;
    claim.owner_id = PM_F1_TIMER_OWNER_TIM4;
    claim.role     = PM_TIMER_ROLE_PWM;
    claim.psc      = t.psc;
    claim.arr      = t.arr;
    claim.channels = 0x04u;     /* CH3 */

    rc = pm_timer_claim(&claim);
    if (rc != PM_OK) {
        return rc;
    }

    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    /* PB8: alternate-function push-pull 50 MHz (TIM4_CH3). */
    GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 0)) | (0xBu << 0);

    TIM4->CR1   = 0u;
    TIM4->PSC   = t.psc;
    TIM4->ARR   = t.arr;
    TIM4->CCR3  = pm_pwm_compare_count(t.arr, cfg->duty_permille);
    TIM4->CCMR2 = (TIM4->CCMR2 & ~(0x7u << 4)) | (0x6u << 4);  /* PWM mode 1 */
    TIM4->CCMR2 |= (0x1u << 3);                                /* OC3 preload */
    TIM4->CCER  |= TIM_CCER_CC3E;
    TIM4->EGR    = TIM_EGR_UG;
    TIM4->SR     = 0u;
    TIM4->CR1    = TIM_CR1_ARPE | TIM_CR1_CEN;

    s_t4.opened   = 1u;
    s_t4.permille = cfg->duty_permille;
    s_t4.psc      = t.psc;
    s_t4.arr      = t.arr;
    s_t4.freq_hz  = t.actual_hz;

    if (actual_hz != NULL) {
        *actual_hz = s_t4.freq_hz;
    }
    return PM_OK;
}

static void tim4_close(void *ctx, pm_pwm_t *p)
{
    (void)ctx;
    (void)p;
    if (s_t4.opened == 0u) {
        return;
    }

    TIM4->CCR3 = 0u;
    TIM4->CCER &= ~TIM_CCER_CC3E;
    TIM4->CR1  &= ~TIM_CR1_CEN;
    (void)pm_timer_release(PM_F1_TIMER_TIM4, PM_F1_TIMER_OWNER_TIM4);
    s_t4.opened   = 0u;
    s_t4.permille = 0u;
    s_t4.freq_hz  = 0u;
}

static pm_err_t tim4_set_freq(void *ctx, pm_pwm_t *p, uint32_t freq_hz,
                              uint32_t *actual_hz)
{
    pm_pwm_timing_t t;
    pm_err_t rc;

    (void)ctx;
    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (p->hw_id != PM_PWM_HW_SERVO) {
        return PM_ERR_RANGE;
    }
    if (s_t4.opened == 0u) {
        return PM_ERR_STATE;
    }

    rc = pm_pwm_timing_compute(TIM4_CLK_HZ, freq_hz, &t);
    if (rc != PM_OK) {
        return rc;
    }
    if (!pm_pwm_timing_matches(TIM4_CLK_HZ, t.psc, t.arr, freq_hz,
                               TIM4_TOL_PERMILLE)) {
        return PM_ERR_PARAM;
    }

    if ((t.psc != s_t4.psc) || (t.arr != s_t4.arr)) {
        rc = pm_timer_retime(PM_F1_TIMER_TIM4, PM_F1_TIMER_OWNER_TIM4,
                             t.psc, t.arr);
        if (rc != PM_OK) {
            return rc;
        }
        TIM4->CR1 &= ~TIM_CR1_CEN;
        TIM4->PSC  = t.psc;
        TIM4->ARR  = t.arr;
        TIM4->CCR3 = pm_pwm_compare_count(t.arr, s_t4.permille);
        TIM4->EGR  = TIM_EGR_UG;
        TIM4->SR   = 0u;
        TIM4->CR1  = TIM_CR1_ARPE | TIM_CR1_CEN;
        s_t4.psc     = t.psc;
        s_t4.arr     = t.arr;
        s_t4.freq_hz = t.actual_hz;
    }

    if (actual_hz != NULL) {
        *actual_hz = s_t4.freq_hz;
    }
    return PM_OK;
}

static pm_err_t tim4_set_duty(void *ctx, pm_pwm_t *p, uint16_t permille)
{
    (void)ctx;
    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (p->hw_id != PM_PWM_HW_SERVO) {
        return PM_ERR_RANGE;
    }
    if (s_t4.opened == 0u) {
        return PM_ERR_STATE;
    }
    if (permille > PM_PWM_DUTY_MAX) {
        return PM_ERR_PARAM;
    }

    s_t4.permille = permille;
    TIM4->CCR3    = pm_pwm_compare_count(s_t4.arr, permille);
    return PM_OK;
}

static pm_err_t tim4_enable(void *ctx, pm_pwm_t *p, bool on)
{
    (void)ctx;
    if (p == NULL) {
        return PM_ERR_NULL;
    }
    if (p->hw_id != PM_PWM_HW_SERVO) {
        return PM_ERR_RANGE;
    }
    if (s_t4.opened == 0u) {
        return PM_ERR_STATE;
    }

    if (on) {
        TIM4->CCER |= TIM_CCER_CC3E;
    } else {
        TIM4->CCER &= ~TIM_CCER_CC3E;
    }
    return PM_OK;
}

const pm_pwm_ops_t pm_f1_tim4_pwm_ops = {
    .open     = tim4_open,
    .close    = tim4_close,
    .set_freq = tim4_set_freq,
    .set_duty = tim4_set_duty,
    .enable   = tim4_enable,
};
