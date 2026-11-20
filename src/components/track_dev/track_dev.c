/*
 * track_dev.c - TRACK command formatting, no register access.
 */
#include "track_dev.h"

#include "pm_track.h"

#define TRACK_REPLY_MAX 48u

static char    s_reply[TRACK_REPLY_MAX];
static uint8_t s_ready;
static pm_track_t s_track;

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

    p    = put_s(p, "\r\n");
    *p   = '\0';
    return s_reply;
}

static const char *reply_raw(void)
{
    bool  on_line = false;
    char *p;

    (void)pm_track_read_raw(&s_track, &on_line);

    /* PB14 module is active low: on_line true means the raw level is 0. */
    p = put_s(s_reply, "TRACK raw=");
    p = put_u32(p, on_line ? 0u : 1u);
    p = put_s(p, "\r\n");
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

void track_dev_init(void)
{
    const pm_track_cfg_t cfg = {
        .hw_id        = 0u,
        .active_level = (uint8_t)PM_GPIO_LEVEL_LOW,
    };

    s_ready = (pm_track_init(&s_track, &cfg) == PM_OK) ? 1u : 0u;
}

const char *track_dev_cmd(const char *line)
{
    const char *p = skip_sp(line);

    if (ci_eq_n(p, "TRACK", 5u) == 0) {
        return 0;
    }
    if ((p[5] != '\0') && (p[5] != ' ')) {
        return 0;
    }
    if (s_ready == 0u) {
        return reply_lit("TRACK err=init");
    }
    p = skip_sp(p + 5);
    if ((*p == '\0') || ((ci_eq_n(p, "STAT", 4u) != 0) && (p[4] == '\0'))) {
        return reply_raw();
    }
    return reply_lit("TRACK err=usage");
}
