/*
 * relay_dev.c - RELAY command parsing and reply formatting. No register
 * access: everything hardware facing goes through pm_relay_*.
 */
#include "relay_dev.h"

#include "pm_relay.h"

#define RELAY_REPLY_MAX 48u

static char    s_reply[RELAY_REPLY_MAX];
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

static const char *reply_lit(const char *s)
{
    char *p = put_s(s_reply, s);

    p  = put_s(p, "\r\n");
    *p = '\0';
    return s_reply;
}

/* pb5= is the level read back from the output register, not the request. */
static const char *reply_level(const char *head)
{
    char *p = put_s(s_reply, head);

    p  = put_s(p, " pb5=");
    p  = put_u32(p, pm_relay_get());
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

void relay_dev_init(void)
{
    s_ready = (pm_relay_init() == 0) ? 1u : 0u;
}

const char *relay_dev_cmd(const char *line)
{
    const char *p = skip_sp(line);

    if (ci_eq_n(p, "RELAY", 5u) == 0) {
        return 0;
    }
    if ((p[5] != '\0') && (p[5] != ' ')) {
        return 0;
    }
    if (s_ready == 0u) {
        return reply_lit("RELAY err=init");
    }
    p = skip_sp(p + 5);
    if (is_word(p, "ON", 2u) != 0) {
        pm_relay_set(1);
        return reply_level("RELAY on ok");
    }
    if (is_word(p, "OFF", 3u) != 0) {
        pm_relay_set(0);
        return reply_level("RELAY off ok");
    }
    if (is_word(p, "STAT", 4u) != 0) {
        return reply_level("RELAY stat");
    }
    return reply_lit("RELAY err=usage");
}
