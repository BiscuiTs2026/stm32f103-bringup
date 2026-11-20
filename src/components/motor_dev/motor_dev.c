/*
 * motor_dev.c - MOTOR command parsing and reply formatting. No register
 * access: everything hardware facing goes through pm_motor_*.
 */
#include "motor_dev.h"

#include <stddef.h>

#include "pm_motor.h"
#include "pm_motor_dbg.h"
#include "pm_pwm.h"

#define MOTOR_REPLY_MAX 96u
#define MOTOR_FREQ_HZ   20000u

static char    s_reply[MOTOR_REPLY_MAX];
static uint8_t s_ready;
static const char *s_init_err;
static motor_dev_dbg_fn s_dbg;

/* Two PWM channels on one timer plus the DRV8833 state machine. */
static pm_pwm_t   s_in1;
static pm_pwm_t   s_in2;
static pm_motor_t s_motor;

/* The model reports coast for both "duty 0" and stop(); this flag keeps the
 * old MOTOR stat wording (dir=stop only after STOP or before the first run). */
static uint8_t s_stopped = 1u;

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
    char *q = &b[sizeof(b) - 1];

    *q = '\0';
    do {
        *--q = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    while (*q != '\0') {
        *p++ = *q++;
    }
    return p;
}

static const char *reply_lit(const char *s)
{
    char *p = put_s(s_reply, s);

    p  = put_s(p, "\r\n");
    *p = '\0';
    return s_reply;
}

static const char *dir_name(pm_motor_dir_t d)
{
    return (d == PM_MOTOR_DIR_REVERSE) ? "rev" : "fwd";
}

/* hz/arr/ccr come from the timer registers, not from the request, so the
 * reply proves what the hardware actually holds. */
static const char *reply_state(const char *head)
{
    pm_motor_hw_t hw;
    char *p;

    pm_motor_hw_snapshot(&hw);
    p = put_s(s_reply, head);
    p = put_s(p, " dir=");
    p = put_s(p, (s_stopped != 0u) ? "stop" : dir_name(pm_motor_dir(&s_motor)));
    p = put_s(p, " duty=");
    p = put_u32(p, (uint32_t)(pm_motor_duty_permille(&s_motor) / 10u));
    p = put_s(p, "% hz=");
    p = put_u32(p, hw.freq_hz);
    p = put_s(p, " arr=");
    p = put_u32(p, hw.arr);
    p = put_s(p, " ccr1=");
    p = put_u32(p, hw.ccr1);
    p = put_s(p, " ccr2=");
    p = put_u32(p, hw.ccr2);
    p = put_s(p, "\r\n");
    *p = '\0';
    return s_reply;
}

static const char *reply_range(uint32_t v)
{
    char *p = put_s(s_reply, "MOTOR err=range duty=");

    p  = put_u32(p, v);
    p  = put_s(p, "\r\n");
    *p = '\0';
    return s_reply;
}

static int ci_eq_n(const char *a, const char *b, unsigned n)
{
    unsigned i;

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
    }
    return 1;
}

static const char *skip_sp(const char *p)
{
    while (*p == ' ') {
        p++;
    }
    return p;
}

/* Subcommand match that also requires a word boundary at the end. */
static int is_word(const char *p, const char *w, unsigned n)
{
    if (ci_eq_n(p, w, n) == 0) {
        return 0;
    }
    return ((p[n] == '\0') || (p[n] == ' ')) ? 1 : 0;
}

/* Decimal 0..1000 accepted; anything else (sign, junk, empty) returns 0. */
static int parse_num(const char *p, uint32_t *out)
{
    uint32_t v = 0u;

    if ((*p < '0') || (*p > '9')) {
        return 0;
    }
    while ((*p >= '0') && (*p <= '9')) {
        v = (v * 10u) + (uint32_t)(*p - '0');
        if (v > 1000u) {
            v = 1000u;
        }
        p++;
    }
    if (*p != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

/* fwd/rev: optional duty, 50 when omitted. */
static const char *do_dir(const char *p, pm_motor_dir_t dir, const char *head)
{
    uint32_t duty = 50u;

    p = skip_sp(p);
    if (*p != '\0') {
        if (parse_num(p, &duty) == 0) {
            return reply_lit("MOTOR err=usage");
        }
        if (duty > 100u) {
            return reply_range(duty);
        }
    }
    (void)pm_motor_set(&s_motor, (uint16_t)(duty * 10u), dir);
    s_stopped = 0u;
    return reply_state(head);
}

void motor_dev_init(void)
{
    pm_pwm_cfg_t   pcfg = {
        .hw_id = 0u, .freq_hz = MOTOR_FREQ_HZ, .duty_permille = 0u
    };
    pm_motor_cfg_t mcfg = {
        .in1 = &s_in1, .in2 = &s_in2, .invert = PM_MOTOR_INVERT_UNSET
    };
    pm_err_t rc;

    rc = pm_pwm_init(&s_in1, &pcfg);
    if (rc == PM_OK) {
        pcfg.hw_id = 1u;
        rc = pm_pwm_init(&s_in2, &pcfg);
    }
    if (rc == PM_OK) {
        rc = pm_motor_init(&s_motor, &mcfg);
    }

    if (rc == PM_OK) {
        s_ready = 1u;
        s_init_err = 0;
        s_stopped = 1u;
    } else {
        s_ready = 0u;
        /* PM_ERR_STATE is the port's remap fact; everything else means the
         * timer could not be taken over, which is the old err=timer wording. */
        s_init_err = (rc == PM_ERR_STATE) ? "MOTOR err=remap" : "MOTOR err=timer";
    }
}

void motor_dev_set_dbg(motor_dev_dbg_fn fn)
{
    s_dbg = fn;
}

const char *motor_dev_cmd(const char *line)
{
    const char *p = skip_sp(line);

    if (ci_eq_n(p, "MOTOR", 5u) == 0) {
        return 0;
    }
    if ((p[5] != '\0') && (p[5] != ' ')) {
        return 0;
    }
    if (s_ready == 0u) {
        return reply_lit(s_init_err);
    }
    p = skip_sp(p + 5);
    if (is_word(p, "FWD", 3u) != 0) {
        return do_dir(p + 3, PM_MOTOR_DIR_FORWARD, "MOTOR fwd ok");
    }
    if (is_word(p, "REV", 3u) != 0) {
        return do_dir(p + 3, PM_MOTOR_DIR_REVERSE, "MOTOR rev ok");
    }
    if (is_word(p, "STOP", 4u) != 0) {
        if (p[4] != '\0') {
            return reply_lit("MOTOR err=usage");
        }
        (void)pm_motor_stop(&s_motor, PM_MOTOR_STOP_COAST);
        s_stopped = 1u;
        return reply_state("MOTOR stop ok");
    }
    if (is_word(p, "DUTY", 4u) != 0) {
        uint32_t duty;

        p = skip_sp(p + 4);
        if (parse_num(p, &duty) == 0) {
            return reply_lit("MOTOR err=usage");
        }
        if (duty > 100u) {
            return reply_range(duty);
        }
        if (s_stopped != 0u) {
            return reply_lit("MOTOR err=idle");
        }
        (void)pm_motor_set(&s_motor, (uint16_t)(duty * 10u),
                           pm_motor_dir(&s_motor));
        return reply_state("MOTOR duty ok");
    }
    if (is_word(p, "STAT", 4u) != 0) {
        if (p[4] != '\0') {
            return reply_lit("MOTOR err=usage");
        }
        return reply_state("MOTOR stat");
    }
    if (is_word(p, "DBG", 3u) != 0) {
        if (p[3] != '\0') {
            return reply_lit("MOTOR err=usage");
        }
        if (s_dbg == NULL) {
            return reply_lit("MOTOR err=unsupported");
        }
        return s_dbg();
    }
    return reply_lit("MOTOR err=usage");
}
