/*
 * motor_dev.h - command interface for the PA0/PA1 motor drive.
 */
#ifndef MOTOR_DEV_H
#define MOTOR_DEV_H

void motor_dev_init(void);

/* Handles lines starting with "MOTOR"; returns NULL for anything else.
 * The returned string is a complete reply including CRLF. */
const char *motor_dev_cmd(const char *line);

#endif /* MOTOR_DEV_H */
