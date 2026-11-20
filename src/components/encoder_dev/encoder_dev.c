/*
 * encoder_dev.c - ENC command formatting, no register access.
 */
#include "encoder_dev.h"

#include "pm_encoder.h"

#define ENC_REPLY_MAX 72u

static char    s_reply[ENC_REPLY_MAX];
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

static char *put_i32(char *p, int32_t v)
{
    if (v < 0) {
        *p++ = '-';
        return put_u32(p, (uint32_t)(-v));
    }
    return put_u32(p, (uint32_t)v);
}

static const char *reply_lit(const char *s)
{
    char *p = put_s(s_reply, s);

    p    = put_s(p, "\r\n");
    *p   = '\0';
    return s_reply;
}

static const char *reply_stat(void)
{
#if PM_ENABLE_ENCODER
    int32_t  step = pm_encoder_last_step();
    uint32_t ab   = pm_encoder_ab();
    char    *p    = put_s(s_reply, "ENC count=");

    p = put_i32(p, pm_encoder_count());
    p = put_s(p, " dir=");
    p = put_s(p, (step > 0) ? "cw" : ((step < 0) ? "ccw" : "idle"));
    p = put_s(p, " a=");
    p = put_u32(p, ab & 1u);
    p = put_s(p, " b=");
    p = put_u32(p, (ab >> 1) & 1u);
    p = put_s(p, " btn=");
    p = put_u32(p, pm_encoder_button());
    p = put_s(p, "\r\n");
    *p = '\0';
    return s_reply;
#else
    /* Unreachable while s_ready is 0, but the port symbols do not exist. */
    return reply_lit("ENC err=init");
#endif
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

void encoder_dev_init(void)
{
#if PM_ENABLE_ENCODER
    s_ready = (pm_encoder_init() == 0) ? 1u : 0u;
#else
    /* Module not built (decision 0018 §6): no init, no EXTI9_5 ISR. Staying
     * "not ready" reuses the existing ENC err=init reply. */
    s_ready = 0u;
#endif
}

const char *encoder_dev_cmd(const char *line)
{
    const char *p = skip_sp(line);

    if (ci_eq_n(p, "ENC", 3u) == 0) {
        return 0;
    }
    if ((p[3] != '\0') && (p[3] != ' ')) {
        return 0;
    }
    if (s_ready == 0u) {
        return reply_lit("ENC err=init");
    }
    p = skip_sp(p + 3);
    if ((*p == '\0') || ((ci_eq_n(p, "STAT", 4u) != 0) && (p[4] == '\0'))) {
        return reply_stat();
    }
    if ((ci_eq_n(p, "CLEAR", 5u) != 0) && (p[5] == '\0')) {
#if PM_ENABLE_ENCODER
        pm_encoder_reset();
#endif
        return reply_lit("ENC clear ok count=0");
    }
    return reply_lit("ENC err=usage");
}
