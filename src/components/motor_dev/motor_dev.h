/*
 * motor_dev.h - command interface for the PA0/PA1 motor drive.
 */
#ifndef MOTOR_DEV_H
#define MOTOR_DEV_H

void motor_dev_init(void);

/* Board diagnostic text for "MOTOR dbg": returns one complete reply line
 * including CRLF, or NULL when no board hook is installed. The entry layer
 * binds a board header (platform/<chip>) implementation here, so the motor
 * contract itself stays free of board details. */
typedef const char *(*motor_dev_dbg_fn)(void);
void motor_dev_set_dbg(motor_dev_dbg_fn fn);

/* Handles lines starting with "MOTOR"; returns NULL for anything else.
 * The returned string is a complete reply including CRLF. */
const char *motor_dev_cmd(const char *line);

#endif /* MOTOR_DEV_H */
