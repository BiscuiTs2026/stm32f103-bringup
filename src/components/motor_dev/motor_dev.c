/*
 * motor_dev.c - MOTOR command parsing and reply formatting. No register
 * access: everything hardware facing goes through pm_motor_*.
 */
#include "motor_dev.h"

#include <stddef.h>

#include "pm_motor.h"

#define MOTOR_REPLY_MAX 96u

static char    s_reply[MOTOR_REPLY_MAX];
static uint8_t s_ready;
static const char *s_init_err;
static motor_dev_dbg_fn s_dbg;

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
    if (d == PM_MOTOR_FWD) {
        return "fwd";
    }
    if (d == PM_MOTOR_REV) {
        return "rev";
    }
    return "stop";
}

/* hz/arr/ccr come from the timer registers, not from the request, so the
 * reply proves what the hardware actually holds. */
static const char *reply_state(const char *head)
{
    pm_motor_stat_t st;
    char *p;

    pm_motor_stat(&st);
    p = put_s(s_reply, head);
    p = put_s(p, " dir=");
    p = put_s(p, dir_name(st.dir));
    p = put_s(p, " duty=");
    p = put_u32(p, st.duty_pct);
    p = put_s(p, "% hz=");
    p = put_u32(p, st.freq_hz);
    p = put_s(p, " arr=");
    p = put_u32(p, st.arr);
    p = put_s(p, " ccr1=");
    p = put_u32(p, st.ccr1);
    p = put_s(p, " ccr2=");
    p = put_u32(p, st.ccr2);
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
    (void)pm_motor_set(dir, (uint8_t)duty);
    return reply_state(head);
}

void motor_dev_init(void)
{
    int rc = pm_motor_init();

    if (rc == 0) {
        s_ready = 1u;
        s_init_err = 0;
    } else {
        s_ready = 0u;
        s_init_err = (rc == -1) ? "MOTOR err=timer" : "MOTOR err=remap";
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
        return do_dir(p + 3, PM_MOTOR_FWD, "MOTOR fwd ok");
    }
    if (is_word(p, "REV", 3u) != 0) {
        return do_dir(p + 3, PM_MOTOR_REV, "MOTOR rev ok");
    }
    if (is_word(p, "STOP", 4u) != 0) {
        if (p[4] != '\0') {
            return reply_lit("MOTOR err=usage");
        }
        pm_motor_stop();
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
        if (pm_motor_duty((uint8_t)duty) != 0) {
            return reply_lit("MOTOR err=idle");
        }
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
