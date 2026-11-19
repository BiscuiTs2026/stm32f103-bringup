/*
 * pwm_dev.c - PWM command parsing and reply formatting. No register access:
 * everything hardware facing goes through pm_pwm_* so this file stays in the
 * component layer and keeps the entry glue free of PWM code.
 */
#include "pwm_dev.h"

#include "pm_pwm.h"

#define PWM_REPLY_MAX 96u

static char    s_reply[PWM_REPLY_MAX];
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
