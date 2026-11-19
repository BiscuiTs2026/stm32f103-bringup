/*
 * pm_cmd.h - stateless helpers shared by the command parser and the reply
 * builders: case-insensitive compare, decimal parsing, fixed-width field
 * formatting. No hardware, no state, host-testable.
 */
#ifndef PM_CMD_H
#define PM_CMD_H

#include <stdint.h>

/* 1 when both strings are equal ignoring ASCII case. */
int pm_cmd_ci_equal(const char *a, const char *b);

/* 1 when line starts with pfx (case-insensitive); *rest points just past it. */
int pm_cmd_ci_starts(const char *line, const char *pfx, const char **rest);

/* Strict decimal: optional spaces, digits, optional spaces, end. */
int pm_cmd_parse_u32(const char *s, uint32_t *out);

/* Write helpers; all return the advanced cursor. */
char *pm_cmd_sput_u32(char *p, uint32_t v);
char *pm_cmd_sput_s(char *p, const char *s, uint8_t width);
char *pm_cmd_sput_pad(char *p, uint32_t v, uint8_t width);
char *pm_cmd_sput_f1(char *p, int32_t v10);

#endif /* PM_CMD_H */
