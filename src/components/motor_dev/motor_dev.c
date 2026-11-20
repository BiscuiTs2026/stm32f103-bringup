/*
 * motor_dev.c - MOTOR command parsing and reply formatting. No register
 * access: everything hardware facing goes through pm_motor_*.
 */
#include "motor_dev.h"

#include <stddef.h>

#include "pm_motor.h"
#include "pm_motor_dbg.h"
#include "pm_pwm.h"
#include "pm_timer.h"

#define MOTOR_REPLY_MAX 96u
#define MOTOR_FREQ_HZ   20000u

/* Board-defined timer ids and a diagnostic owner that is never used by the
 * firmware itself. Must match src/port/stm32f1/pm_timer_stm32f1.h. */
#define MOTOR_TIM2_ID    2u
#define MOTOR_TIM3_ID    3u
#define MOTOR_TIM4_ID    4u
#define MOTOR_DIAG_OWNER 0x7Eu

#define MOTOR_DIAG_MAX  448u

static char    s_reply[MOTOR_REPLY_MAX];
static char    s_diag[MOTOR_DIAG_MAX];
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

/* ---- claim-table diagnostics (hidden: not listed in HELP) -----------------
 * These subcommands exist to prove, from replies alone, that arbitration is
 * done by the model's table rather than by the old CEN shortcut:
 *   MOTOR claims   dump pm_timer_claim_at() for TIM2/TIM3/TIM4
 *   MOTOR conflict second claim on TIM2 (motor holds it) and a compatible
 *                  fourth channel on TIM4
 *   MOTOR busy     claim TIM3 while the buzzer has the counter running
 * None of them changes the text of an existing command. */

static const char *err_name(pm_err_t e)
{
    switch (e) {
    case PM_OK:           return "ok";
    case PM_ERR_BUSY:     return "busy";
    case PM_ERR_CONFLICT: return "conflict";
    case PM_ERR_STATE:    return "state";
    case PM_ERR_PARAM:    return "param";
    case PM_ERR_NOMEM:    return "nomem";
    case PM_ERR_RANGE:    return "range";
    case PM_ERR_NULL:     return "null";
    default:              return "other";
    }
}

static const char *role_name(pm_timer_role_t r)
{
    switch (r) {
    case PM_TIMER_ROLE_PWM:     return "pwm";
    case PM_TIMER_ROLE_TONE:    return "tone";
    case PM_TIMER_ROLE_COUNTER: return "counter";
    default:                    return "?";
    }
}

static char *put_hex8(char *p, uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";

    *p++ = hex[(v >> 4) & 0xFu];
    *p++ = hex[v & 0xFu];
    return p;
}

static const char *reply_claims(void)
{
    char    *p = s_diag;
    uint8_t  hw;

    p = put_s(p, "MOTOR claims");
    for (hw = MOTOR_TIM2_ID; hw <= MOTOR_TIM4_ID; hw++) {
        uint8_t n = pm_timer_claims(hw);
        uint8_t i;

        p = put_s(p, " t");
        p = put_u32(p, hw);
        p = put_s(p, "=");
        p = put_u32(p, n);
        for (i = 0u; i < n; i++) {
            pm_timer_claim_t c;

            if (pm_timer_claim_at(hw, i, &c) != PM_OK) {
                p = put_s(p, " ?");
                break;
            }
            p = put_s(p, " o=");
            p = put_u32(p, c.owner_id);
            p = put_s(p, " r=");
            p = put_s(p, role_name(c.role));
            p = put_s(p, " psc=");
            p = put_u32(p, c.psc);
            p = put_s(p, " arr=");
            p = put_u32(p, c.arr);
            p = put_s(p, " ch=0x");
            p = put_hex8(p, c.channels);
        }
    }
    p  = put_s(p, "\r\n");
    *p = '\0';
    return s_diag;
}

static const char *reply_conflict(void)
{
    char            *p = s_diag;
    pm_timer_claim_t c;
    pm_err_t         rc2;
    pm_err_t         rc4 = PM_ERR_STATE;
    pm_err_t         rel = PM_ERR_STATE;
    uint8_t          n4;
    uint16_t         psc4 = 0u;
    uint16_t         arr4 = 0u;

    n4 = pm_timer_claims(MOTOR_TIM4_ID);
    if ((n4 > 0u) && (pm_timer_claim_at(MOTOR_TIM4_ID, 0u, &c) == PM_OK)) {
        psc4 = c.psc;
        arr4 = c.arr;
    } else {
        n4 = 0u;
    }

    /* Same hw_id as the motor, different owner and role: the model must say
     * CONFLICT, not overwrite the motor's time base. */
    c.hw_id    = MOTOR_TIM2_ID;
    c.owner_id = MOTOR_DIAG_OWNER;
    c.role     = PM_TIMER_ROLE_TONE;
    c.psc      = 0u;
    c.arr      = PM_TIMER_ARR_FULL;
    c.channels = 0u;
    rc2 = pm_timer_claim(&c);
    if (rc2 == PM_OK) {
        (void)pm_timer_release(MOTOR_TIM2_ID, MOTOR_DIAG_OWNER);
    }

    /* Positive case: same PSC/ARR as the servo claim, disjoint channel. */
    if (n4 > 0u) {
        c.hw_id    = MOTOR_TIM4_ID;
        c.owner_id = MOTOR_DIAG_OWNER;
        c.role     = PM_TIMER_ROLE_PWM;
        c.psc      = psc4;
        c.arr      = arr4;
        c.channels = 0x08u;     /* CH4, servo uses CH3 */
        rc4 = pm_timer_claim(&c);
        if (rc4 == PM_OK) {
            rel = pm_timer_release(MOTOR_TIM4_ID, MOTOR_DIAG_OWNER);
        }
    }

    p = put_s(p, "MOTOR conflict t2=");
    p = put_s(p, err_name(rc2));
    p = put_s(p, " t4=");
    p = put_s(p, err_name(rc4));
    p = put_s(p, " rel=");
    p = put_s(p, err_name(rel));
    p  = put_s(p, "\r\n");
    *p = '\0';
    return s_diag;
}

static const char *reply_busy(void)
{
    char            *p = s_diag;
    pm_timer_claim_t c;
    pm_err_t         rc;
    pm_err_t         rel = PM_ERR_STATE;

    /* TIM3 is deliberately outside the table; with the buzzer running the
     * probe sees CEN and the model must refuse with BUSY. */
    c.hw_id    = MOTOR_TIM3_ID;
    c.owner_id = MOTOR_DIAG_OWNER;
    c.role     = PM_TIMER_ROLE_TONE;
    c.psc      = 71u;
    c.arr      = 999u;
    c.channels = 0u;
    rc = pm_timer_claim(&c);
    if (rc == PM_OK) {
        rel = pm_timer_release(MOTOR_TIM3_ID, MOTOR_DIAG_OWNER);
    }

    p = put_s(p, "MOTOR busy t3=");
    p = put_s(p, err_name(rc));
    p = put_s(p, " rel=");
    p = put_s(p, err_name(rel));
    p  = put_s(p, "\r\n");
    *p = '\0';
    return s_diag;
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
    if (is_word(p, "BRAKE", 5u) != 0) {
        if (p[5] != '\0') {
            return reply_lit("MOTOR err=usage");
        }
        /* Both DRV8833 inputs to 1000 permille (L/L). The model already
         * coasts both channels before any later set(), so a following
         * fwd/rev leaves the brake with no residual 1000. */
        (void)pm_motor_stop(&s_motor, PM_MOTOR_STOP_BRAKE);
        s_stopped = 1u;
        return reply_state("MOTOR brake ok");
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
    if (is_word(p, "CLAIMS", 6u) != 0) {
        if (p[6] != '\0') {
            return reply_lit("MOTOR err=usage");
        }
        return reply_claims();
    }
    if (is_word(p, "CONFLICT", 8u) != 0) {
        if (p[8] != '\0') {
            return reply_lit("MOTOR err=usage");
        }
        return reply_conflict();
    }
    if (is_word(p, "BUSY", 4u) != 0) {
        if (p[4] != '\0') {
            return reply_lit("MOTOR err=usage");
        }
        return reply_busy();
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
