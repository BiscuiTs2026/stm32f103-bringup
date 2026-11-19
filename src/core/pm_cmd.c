/*
 * pm_cmd.c - stateless parser/formatting helpers lifted out of main.c.
 * Byte-for-byte the same algorithms; only the names gained a pm_cmd_ prefix.
 */
#include "pm_cmd.h"

#include <stddef.h>

int pm_cmd_ci_equal(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        char ca = *a++;
        char cb = *b++;

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
    return (*a == '\0') && (*b == '\0');
}

int pm_cmd_ci_starts(const char *line, const char *pfx, const char **rest)
{
    const char *p = line;

    while (*pfx != '\0') {
        char a = *p;
        char b = *pfx;

        if ((a >= 'a') && (a <= 'z')) {
            a = (char)(a - 'a' + 'A');
        }
        if ((b >= 'a') && (b <= 'z')) {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
        p++;
        pfx++;
    }
    *rest = p;
    return 1;
}

int pm_cmd_parse_u32(const char *s, uint32_t *out)
{
    uint32_t v   = 0u;
    int      any = 0;

    while (*s == ' ') {
        s++;
    }
    while ((*s >= '0') && (*s <= '9')) {
        v = (v * 10u) + (uint32_t)(*s - '0');
        any = 1;
        s++;
    }
    if (any == 0) {
        return 0;
    }
    while (*s == ' ') {
        s++;
    }
    if (*s != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

char *pm_cmd_sput_u32(char *p, uint32_t v)
{
    char   tmp[11];
    size_t n = 0u;

    do {
        tmp[n] = (char)('0' + (v % 10u));
        v /= 10u;
        n++;
    } while (v != 0u);
    while (n != 0u) {
        n--;
        *p++ = tmp[n];
    }
    return p;
}

char *pm_cmd_sput_s(char *p, const char *s, uint8_t width)
{
    uint8_t n = 0u;

    while ((s[n] != '\0') && (n < width)) {
        *p++ = s[n];
        n++;
    }
    while (n < width) {
        *p++ = ' ';
        n++;
    }
    return p;
}

char *pm_cmd_sput_pad(char *p, uint32_t v, uint8_t width)
{
    char     tmp[11];
    uint8_t  n = 0u;

    do {
        tmp[n] = (char)('0' + (v % 10u));
        v /= 10u;
        n++;
    } while (v != 0u);
    while (n < width) {
        *p++ = ' ';
        width--;
    }
    while (n != 0u) {
        n--;
        *p++ = tmp[n];
    }
    return p;
}

char *pm_cmd_sput_f1(char *p, int32_t v10)
{
    if (v10 < 0) {
        if (v10 < -99) {
            v10 = -99;
        }
        *p++ = '-';
        *p++ = (char)('0' + ((-v10) / 10));
        *p++ = '.';
        *p++ = (char)('0' + ((-v10) % 10));
        return p;
    }
    if (v10 > 999) {
        v10 = 999;
    }
    if (v10 >= 100) {
        *p++ = (char)('0' + (v10 / 100));
    } else {
        *p++ = ' ';
    }
    *p++ = (char)('0' + ((v10 % 100) / 10));
    *p++ = '.';
    *p++ = (char)('0' + (v10 % 10));
    return p;
}
