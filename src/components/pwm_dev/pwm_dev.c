/*
 * pwm_dev.c - PWM command parsing and reply formatting. No register access:
 * everything hardware facing goes through pm_pwm_* so this file stays in the
 * component layer and keeps the entry glue free of PWM code.
 */
#include "pwm_dev.h"

#include "pm_osal.h"
#include "pm_pwm.h"

#define PWM_REPLY_MAX 384u
#define PWM_SWEEP_MAX 768u

static char    s_reply[PWM_REPLY_MAX];
static char    s_sweep[PWM_SWEEP_MAX];
static uint8_t s_ready;

static char *put_s(char *p, const char *s)
{
    while (*s != '\0') {
        *p++ = *s++;
    }
    return p;
}

static char *put_u32(char *p, uint32_t v)
{
    char  b[11];
    char *q = &b[sizeof(b) - 1u];

    *q = '\0';
    do {
        *--q = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    return put_s(p, q);
}

/* 0.1 unit fixed point, always one decimal: 90.0, 7.5, 180.0. */
static char *put_deg(char *p, uint32_t deg10)
{
    p = put_u32(p, deg10 / 10u);
    *p++ = '.';
    *p++ = (char)('0' + (deg10 % 10u));
    return p;
}

/* Fixed width 0xXXXXXXXX so register dumps line up and are easy to compare. */
static char *put_hex32(char *p, uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int32_t           sh;

    p = put_s(p, "0x");
    for (sh = 28; sh >= 0; sh -= 4) {
        *p++ = hex[(v >> sh) & 0xFu];
    }
    return p;
}

static const char *skip_sp(const char *p)
{
    while (*p == ' ') {
        p++;
    }
    return p;
}

static int ci_eq_n(const char *a, const char *b, uint32_t n)
{
    uint32_t i;

    for (i = 0u; i < n; i++) {
        char ca = a[i];
        char cb = b[i];

        if ((ca >= 'a') && (ca <= 'z')) {
            ca = (char)(ca - 'a' + 'A');
        }
        if ((cb >= 'a') && (cb <= 'z')) {
            cb = (char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return 0;
        }
        if (cb == '\0') {
            return 0;
        }
    }
    return 1;
}

/* Reads one decimal number. Returns 0 when there is no digit at *pp. */
static int take_u32(const char **pp, uint32_t *out)
{
    const char *p = skip_sp(*pp);
    uint32_t    v = 0u;
    uint8_t     any = 0u;

    while ((*p >= '0') && (*p <= '9')) {
        if (v > 100000u) {
            return 0;
        }
        v = (v * 10u) + (uint32_t)(*p - '0');
        p++;
        any = 1u;
    }
    if (any == 0u) {
        return 0;
    }
    *out = v;
    *pp  = p;
    return 1;
}

/* Terminates the line in s_reply with CRLF. Builders keep it well under the
 * buffer size, so this only walks to the existing terminator. */
static const char *finish(void)
{
    char *p = s_reply;

    while (*p != '\0') {
        p++;
    }
    *p++ = '\r';
    *p++ = '\n';
    *p   = '\0';
    return s_reply;
}

static const char *reply_lit(const char *s)
{
    char       *p = s_reply;
    const char *q = s;

    while ((*q != '\0') && (p < &s_reply[PWM_REPLY_MAX - 3u])) {
        *p++ = *q++;
    }
    *p++ = '\r';
    *p++ = '\n';
    *p   = '\0';
    return s_reply;
}

static const char *reply_servo(void)
{
    char *p = s_reply;

    p = put_s(p, "PWM servo ok deg=");
    p = put_deg(p, (uint32_t)pm_pwm_servo_deg10());
    p = put_s(p, " pulse=");
    p = put_u32(p, pm_pwm_servo_pulse_us());
    p = put_s(p, "us");
    *p = '\0';
    return finish();
}

static const char *reply_buz(void)
{
    char *p = s_reply;

    p = put_s(p, "PWM buz ok freq=");
    p = put_u32(p, pm_pwm_buzzer_hz());
    p = put_s(p, "Hz duty=");
    p = put_u32(p, (uint32_t)pm_pwm_buzzer_duty());
    p = put_s(p, "%");
    *p = '\0';
    return finish();
}

static const char *cmd_servo(const char *p)
{
    uint32_t deg;

    if (take_u32(&p, &deg) == 0) {
        return reply_lit("PWM err=usage");
    }
    if (deg > 180u) {
        char *q = s_reply;

        q = put_s(q, "PWM err=range deg=");
        q = put_u32(q, deg);
        *q = '\0';
        return finish();
    }
    if (pm_pwm_servo_set((int32_t)(deg * 10u)) != 0) {
        return reply_lit("PWM err=range");
    }
    return reply_servo();
}

static const char *cmd_buz(const char *p)
{
    uint32_t hz;
    uint32_t duty = PM_PWM_BUZ_DUTY_DEFAULT;

    p = skip_sp(p);
    if (ci_eq_n(p, "off", 3u) != 0) {
        pm_pwm_buzzer_off();
        return reply_lit("PWM buz off ok");
    }
    if (take_u32(&p, &hz) == 0) {
        return reply_lit("PWM err=usage");
    }
    if (take_u32(&p, &duty) != 0) {
        if (duty > 99u) {
            return reply_lit("PWM err=range duty");
        }
    }
    if ((hz < PM_PWM_BUZ_MIN_HZ) || (hz > PM_PWM_BUZ_MAX_HZ)) {
        char *q = s_reply;

        q = put_s(q, "PWM err=range hz=");
        q = put_u32(q, hz);
        *q = '\0';
        return finish();
    }
    if (pm_pwm_buzzer_set(hz, (uint8_t)duty) != 0) {
        return reply_lit("PWM err=range");
    }
    return reply_buz();
}

/* Slow 0->180->0 sweep for visual confirmation: 300 ms per step, two rounds,
 * then back to 90 degrees. Every step is appended to s_sweep so the whole
 * sequence leaves the port as one multi-line reply; main.c is not involved. */
static const uint8_t s_sweep_deg[] = {
    0u, 45u, 90u, 135u, 180u, 135u, 90u, 45u, 0u
};

static const char *cmd_sweep(void)
{
    char    *p = s_sweep;
    uint32_t round;
    uint32_t i;

    for (round = 0u; round < 2u; round++) {
        for (i = 0u; i < (uint32_t)sizeof(s_sweep_deg); i++) {
            if (pm_pwm_servo_set((int32_t)s_sweep_deg[i] * 10) != 0) {
                return reply_lit("PWM err=range");
            }
            p = put_s(p, "PWM sweep deg=");
            p = put_deg(p, (uint32_t)pm_pwm_servo_deg10());
            p = put_s(p, " pulse=");
            p = put_u32(p, pm_pwm_servo_pulse_us());
            p = put_s(p, "us\r\n");
            pm_osal_delay_ms(300u);
        }
    }
    (void)pm_pwm_servo_set(900);
    p = put_s(p, "PWM sweep deg=90.0 pulse=");
    p = put_u32(p, pm_pwm_servo_pulse_us());
    p = put_s(p, "us\r\nPWM sweep done\r\n");
    *p = '\0';
    return s_sweep;
}

/* Read-only register snapshot: proves what the timer and pin config contain.
 * STM32F1 has no MODER/OSPEEDR/AFRH, so PB8 mode is shown as GPIOB->CRH (low
 * nibble) and remap state as AFIO->MAPR. Does not change any configuration. */
static const char *cmd_dbg(void)
{
    pm_pwm_dbg_t d;
    uint32_t     c0;
    uint32_t     c1;
    char        *q = s_reply;

    pm_pwm_dbg_read(&d);
    c0 = pm_pwm_counter();
    pm_osal_delay_ms(20u);
    c1 = pm_pwm_counter();

    q = put_s(q, "PWM dbg apb1enr=");
    q = put_hex32(q, d.rcc_apb1enr);
    q = put_s(q, " apb2enr=");
    q = put_hex32(q, d.rcc_apb2enr);
    q = put_s(q, " cr1=");
    q = put_hex32(q, d.tim4_cr1);
    q = put_s(q, " psc=");
    q = put_hex32(q, d.tim4_psc);
    q = put_s(q, " arr=");
    q = put_hex32(q, d.tim4_arr);
    q = put_s(q, " ccmr2=");
    q = put_hex32(q, d.tim4_ccmr2);
    q = put_s(q, " ccer=");
    q = put_hex32(q, d.tim4_ccer);
    q = put_s(q, " ccr3=");
    q = put_hex32(q, d.tim4_ccr3);
    q = put_s(q, " cnt0=");
    q = put_u32(q, c0);
    q = put_s(q, " cnt1=");
    q = put_u32(q, c1);
    q = put_s(q, " dt=20ms crh=");
    q = put_hex32(q, d.gpiob_crh);
    q = put_s(q, " mapr=");
    q = put_hex32(q, d.afio_mapr);
    *q = '\0';
    return finish();
}

/* 1 Hz (500 ms high, 500 ms low) on PB8 as a plain GPIO for 10 s, then back to
 * TIM4 alternate function and 90 degrees. The multimeter measures the average
 * voltage on the signal pin: 1.65 V means the MCU drives it, 0/3.3 V means not. */
static const char *cmd_test(void)
{
    uint32_t i;
    char    *q;

    pm_pwm_test_begin();
    for (i = 0u; i < 10u; i++) {
        pm_pwm_test_level(1);
        pm_osal_delay_ms(500u);
        pm_pwm_test_level(0);
        pm_osal_delay_ms(500u);
    }
    pm_pwm_test_end();

    q = s_reply;
    q = put_s(q, "PWM test done 1Hz 10s pb8=af servo=");
    q = put_deg(q, (uint32_t)pm_pwm_servo_deg10());
    q = put_s(q, " pulse=");
    q = put_u32(q, pm_pwm_servo_pulse_us());
    q = put_s(q, "us");
    *q = '\0';
    return finish();
}

void pwm_dev_init(void)
{
    s_ready = (pm_pwm_init() == 0) ? 1u : 0u;
}

const char *pwm_dev_cmd(const char *line)
{
    const char *p;
    char       *q;

    p = skip_sp(line);
    if (ci_eq_n(p, "PWM", 3u) == 0) {
        return 0;
    }
    if ((p[3] != ' ') && (p[3] != '\0')) {
        return 0;
    }
    if (s_ready == 0u) {
        return reply_lit("PWM err=init");
    }

    p = skip_sp(p + 3);
    if (ci_eq_n(p, "SERVO", 5u) != 0) {
        return cmd_servo(p + 5);
    }
    if (ci_eq_n(p, "BUZ", 3u) != 0) {
        return cmd_buz(p + 3);
    }
    if (ci_eq_n(p, "SWEEP", 5u) != 0) {
        return cmd_sweep();
    }
    if (ci_eq_n(p, "DBG", 3u) != 0) {
        return cmd_dbg();
    }
    if (ci_eq_n(p, "TEST", 4u) != 0) {
        return cmd_test();
    }
    if (ci_eq_n(p, "STAT", 4u) != 0) {
        q = s_reply;
        q = put_s(q, "PWM stat servo=");
        q = put_deg(q, (uint32_t)pm_pwm_servo_deg10());
        q = put_s(q, "c pulse=");
        q = put_u32(q, pm_pwm_servo_pulse_us());
        q = put_s(q, "us buz=");
        if (pm_pwm_buzzer_on() != 0) {
            q = put_u32(q, pm_pwm_buzzer_hz());
            q = put_s(q, "Hz/");
            q = put_u32(q, (uint32_t)pm_pwm_buzzer_duty());
            q = put_s(q, "%");
        } else {
            q = put_s(q, "off");
        }
        *q = '\0';
        return finish();
    }
    return reply_lit("PWM err=usage");
}
