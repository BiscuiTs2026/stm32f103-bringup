/*
 * pm_cmd_table.c - three-pass command routing over a static entry table.
 *
 * Pass 1 runs every PM_CMD_EXACT entry, pass 2 every PM_CMD_TRY entry (the
 * first non-NULL reply wins), pass 3 every PM_CMD_PREFIX entry. That is the
 * same precedence the old hand-written if/else-if chain had: names first, the
 * *_dev_cmd consumers next, "OLED <arg>" last.
 */
#include "pm_cmd_table.h"
#include "pm_cmd.h"

#include <string.h>

void pm_cmd_dispatch(const pm_cmd_entry_t *t, size_t n, cmd_port_t *c,
                     const char *line)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        if ((t[i].kind == PM_CMD_EXACT) && (t[i].run != NULL) &&
            (pm_cmd_ci_equal(line, t[i].name) != 0)) {
            t[i].run(c, line);
            return;
        }
    }

    for (i = 0u; i < n; i++) {
        if ((t[i].kind == PM_CMD_TRY) && (t[i].try_run != NULL)) {
            const char *reply = t[i].try_run(line);

            if (reply != NULL) {
                pm_cmd_out_tx_str(c, reply);
                return;
            }
        }
    }

    for (i = 0u; i < n; i++) {
        if ((t[i].kind == PM_CMD_PREFIX) && (t[i].match != NULL) &&
            (t[i].run_arg != NULL)) {
            const char *arg = t[i].match(line);

            if (arg != NULL) {
                t[i].run_arg(c, arg);
                return;
            }
        }
    }

    pm_cmd_out_tx_str(c, "ERR unknown=");
    pm_cmd_out_tx_str(c, line);
    pm_cmd_out_tx_str(c, "\r\n");
}

int pm_cmd_help_line(const pm_cmd_entry_t *t, size_t n, size_t i,
                     char *buf, size_t cap)
{
    static const char prefix[] = "HELP ";
    size_t            plen = sizeof(prefix) - 1u;
    size_t            hlen;
    size_t            need;

    if ((t == NULL) || (buf == NULL) || (i >= n) || (t[i].help == NULL)) {
        return -1;
    }
    hlen = strlen(t[i].help);
    need = plen + hlen + 2u;           /* prefix + help + CRLF */
    if (cap < (need + 1u)) {           /* plus the NUL terminator */
        return -1;
    }
    memcpy(buf, prefix, plen);
    memcpy(&buf[plen], t[i].help, hlen);
    buf[plen + hlen]      = '\r';
    buf[plen + hlen + 1u] = '\n';
    buf[need]             = '\0';
    return 0;
}

static int entry_name_ok(const char *name)
{
    const char *p;

    if ((name == NULL) || (*name == '\0')) {
        return 0;
    }
    for (p = name; *p != '\0'; p++) {
        if (((*p < 'A') || (*p > 'Z')) && ((*p < '0') || (*p > '9'))) {
            return 0;
        }
    }
    return 1;
}

static int entry_help_ok(const char *help)
{
    const char *p;

    if ((help == NULL) || (*help == '\0')) {
        return 0;
    }
    for (p = help; *p != '\0'; p++) {
        unsigned char ch = (unsigned char)*p;

        if ((ch < 0x20u) || (ch == 0x7Fu)) {
            return 0;
        }
    }
    return 1;
}

static int entry_fields_ok(const pm_cmd_entry_t *e)
{
    switch (e->kind) {
    case PM_CMD_EXACT:
        return (e->run != NULL) && (e->try_run == NULL) &&
               (e->match == NULL) && (e->run_arg == NULL);
    case PM_CMD_TRY:
        return (e->try_run != NULL) && (e->run == NULL) &&
               (e->match == NULL) && (e->run_arg == NULL);
    case PM_CMD_PREFIX:
        return (e->match != NULL) && (e->run_arg != NULL) &&
               (e->run == NULL) && (e->try_run == NULL);
    default:
        return 0;
    }
}

int pm_cmd_table_validate(const pm_cmd_entry_t *t, size_t n)
{
    int    bad = 0;
    size_t i;
    size_t j;

    if (t == NULL) {
        return 1;
    }
    for (i = 0u; i < n; i++) {
        if (!entry_name_ok(t[i].name) || !entry_help_ok(t[i].help) ||
            !entry_fields_ok(&t[i])) {
            bad++;
        }
    }
    for (i = 0u; i < n; i++) {
        for (j = i + 1u; j < n; j++) {
            if ((t[i].name != NULL) && (t[j].name != NULL) &&
                (strcmp(t[i].name, t[j].name) == 0)) {
                bad++;
                break;             /* count each duplicated entry once */
            }
        }
    }
    return bad;
}
