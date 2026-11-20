/*
 * pm_motor_stm32f1.c - DRV8833 drive on PA0/PA1 = TIM2_CH1/CH2.
 *
 * TIM2 runs at 72 MHz (APB1 36 MHz x2): PSC 0 and ARR 3599 give a 20 kHz
 * frame, so the load hears nothing and duty resolution is 1/3600. Only one
 * channel carries the PWM at a time; the other stays at 0, which is the
 * DRV8833 "drive" combination. Both low is coast.
 *
 * TIM2 was the HC-SR04 time base until 2026-11-19 (DIST is disabled now), so
 * no other module is allowed to write PSC/ARR/CNT here.
 */
#include "pm_motor.h"

#include "stm32f1xx.h"

#include "pm_timer.h"
#include "pm_timebase.h"

#define MOTOR_TIM_CLK_HZ  72000000u
#define MOTOR_PSC         0u
#define MOTOR_ARR         ((MOTOR_TIM_CLK_HZ / PM_MOTOR_FREQ_HZ) - 1u)  /* 3599 */
#define MOTOR_TOL_PERMILLE 10u       /* decision 0018: 10 permille */
#define MOTOR_CCMR1_PWM   ((0x6u << 4) | (0x1u << 3) | (0x6u << 12) | (0x1u << 11))

static pm_motor_dir_t s_dir;
static uint8_t        s_duty;

static uint32_t duty_to_ccr(uint8_t pct)
{
    return ((uint32_t)pct * (MOTOR_ARR + 1u)) / 100u;
}

int pm_motor_init(void)
{
    /* Same 10 permille re-check as the servo: a wrong prescaler or a wrong
     * clock assumption must stop here instead of producing an audible frame.
     * Nothing is written on failure and PSC/ARR are not changed. A failure
     * means the constants are wrong, so it reports as err=timer (-1); this
     * round does not add a new error code to the component mapping. */
    if (pm_timebase_ok(MOTOR_TIM_CLK_HZ, MOTOR_PSC, MOTOR_ARR,
                       PM_MOTOR_FREQ_HZ, MOTOR_TOL_PERMILLE) == 0) {
        return -1;
    }

    /* "Occupied" means someone else's time base, decided by two register
     * facts only: TIM2 already counting (CR1.CEN) or CH1/CH2 moved away from
     * PA0/PA1 (AFIO_MAPR.TIM2_REMAP). No software owner flags. The probe is
     * the shared port helper added for the pm_timer contract (decision 0018);
     * the -1/-2 split keeps the existing MOTOR err=timer / err=remap wording,
     * with CEN winning when both facts are set, as before. */
    uint32_t busy = pm_timer_probe(PM_TIMER_T2);

    if (busy != PM_TIMER_FREE) {
        return ((busy & PM_TIMER_BUSY_CEN) != 0u) ? -1 : -2;
    }

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /* PA0 (CRL 3:0) and PA1 (CRL 7:4): alternate-function push-pull 50 MHz. */
    GPIOA->BRR = (1u << 0) | (1u << 1);
    GPIOA->CRL = (GPIOA->CRL & ~((0xFu << 0) | (0xFu << 4)))
               | (0xBu << 0) | (0xBu << 4);

    TIM2->CR1   = 0u;
    TIM2->PSC   = MOTOR_PSC;
    TIM2->ARR   = MOTOR_ARR;
    TIM2->CCR1  = 0u;
    TIM2->CCR2  = 0u;
    /* OC1/OC2 PWM mode 1 with preload so a duty write takes effect on the
     * next update event instead of tearing the current pulse. */
    TIM2->CCMR1 = MOTOR_CCMR1_PWM;
    TIM2->CCER  = TIM_CCER_CC1E | TIM_CCER_CC2E;
    TIM2->EGR   = TIM_EGR_UG;
    TIM2->SR    = 0u;
    TIM2->CR1   = TIM_CR1_ARPE | TIM_CR1_CEN;

    s_dir  = PM_MOTOR_STOP;
    s_duty = 0u;
    return 0;
}

void pm_motor_stop(void)
{
    /* Inactive channel first, active channel second, so the two high-side
     * inputs are never on together even for one 50 us frame. */
    TIM2->CCR1 = 0u;
    TIM2->CCR2 = 0u;
    s_dir  = PM_MOTOR_STOP;
    s_duty = 0u;
}

int pm_motor_set(pm_motor_dir_t dir, uint8_t duty_pct)
{
    uint32_t ccr;

    if (duty_pct > 100u) {
        return -1;
    }
    if (dir == PM_MOTOR_STOP) {
        pm_motor_stop();
        return 0;
    }
    if ((dir != PM_MOTOR_FWD) && (dir != PM_MOTOR_REV)) {
        return -1;
    }

    ccr = duty_to_ccr(duty_pct);
    if (dir == PM_MOTOR_FWD) {
        TIM2->CCR2 = 0u;
        TIM2->CCR1 = ccr;
    } else {
        TIM2->CCR1 = 0u;
        TIM2->CCR2 = ccr;
    }
    s_dir  = dir;
    s_duty = duty_pct;
    return 0;
}

int pm_motor_duty(uint8_t duty_pct)
{
    if (s_dir == PM_MOTOR_STOP) {
        return -1;
    }
    return pm_motor_set(s_dir, duty_pct);
}

void pm_motor_stat(pm_motor_stat_t *out)
{
    uint32_t div;

    out->psc  = (uint16_t)TIM2->PSC;
    out->arr  = TIM2->ARR;
    div       = ((uint32_t)out->psc + 1u) * (out->arr + 1u);
    out->freq_hz = (div == 0u) ? 0u : (MOTOR_TIM_CLK_HZ / div);

    out->ccr1     = TIM2->CCR1;
    out->ccr2     = TIM2->CCR2;
    out->duty_pct = s_duty;
    out->dir      = s_dir;
}
