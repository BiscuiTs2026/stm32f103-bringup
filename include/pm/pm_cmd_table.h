/*
 * pm_cmd_table.h - table-driven command routing.
 *
 * The entry table itself stays next to the handlers (main.c for now); this
 * module only knows how to route a line through it. Routing keeps the old
 * if/else-if precedence without hard-coding it: three passes over the table in
 * declaration order - exact names, then commands that try to consume the line,
 * then prefix commands that take an argument. No I/O and no hardware, so the
 * whole module is host-testable.
 */
#ifndef PM_CMD_TABLE_H
#define PM_CMD_TABLE_H

#include <stddef.h>

#include "pm_cmd_out.h"

typedef enum {
    PM_CMD_EXACT = 0,   /* line must equal name (case-insensitive) */
    PM_CMD_TRY,         /* try_run(line) returns a reply, or NULL to pass */
    PM_CMD_PREFIX       /* match(line) returns the argument, or NULL */
} pm_cmd_kind_t;

typedef struct {
    const char   *name;   /* upper-case token: EXACT lookup key, validation id */
    const char   *help;   /* text after "HELP ", copied verbatim into HELP */
    pm_cmd_kind_t kind;

    void        (*run)(cmd_port_t *c, const char *line);
    const char *(*try_run)(const char *line);
    const char *(*match)(const char *line);
    void        (*run_arg)(cmd_port_t *c, const char *arg);
} pm_cmd_entry_t;

/* Route one command line. Unknown lines get "ERR unknown=<line>". */
void pm_cmd_dispatch(const pm_cmd_entry_t *t, size_t n, cmd_port_t *c, const char *line);

/* Build line i as "HELP " + t[i].help + "\r\n", NUL-terminated. Returns 0, or
 * -1 when i is out of range or cap cannot hold the line plus its terminator. */
int pm_cmd_help_line(const pm_cmd_entry_t *t, size_t n, size_t i, char *buf, size_t cap);

/* Count table problems: empty or non-upper-case names, duplicates, empty help
 * or help with control characters, and fields that do not match the entry's
 * kind. 0 means the table is consistent. */
int pm_cmd_table_validate(const pm_cmd_entry_t *t, size_t n);

#endif /* PM_CMD_TABLE_H */
