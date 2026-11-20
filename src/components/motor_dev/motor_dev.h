/*
 * motor_dev.h - command interface for the PA0/PA1 motor drive.
 */
#ifndef MOTOR_DEV_H
#define MOTOR_DEV_H

#include <stdint.h>

void motor_dev_init(void);

/* Board diagnostic text for "MOTOR dbg": returns one complete reply line
 * including CRLF, or NULL when no board hook is installed. The entry layer
 * binds a board header (platform/<chip>) implementation here, so the motor
 * contract itself stays free of board details. */
typedef const char *(*motor_dev_dbg_fn)(void);
void motor_dev_set_dbg(motor_dev_dbg_fn fn);

/* Debug-only fault injector behind "MOTOR force timer|remap". mode 0 starts
 * TIM2 outside the model (expect err=timer), mode 1 sets the AFIO TIM2 remap
 * field (expect err=remap). The board implementation must restore registers
 * and the claim table. *rc receives the pm_err_t classification, *bits the
 * observed probe bits, *before/*after the claim count around the operation.
 * Returns 0 done, -1 precondition, -2 restore failed. Do not rely on this in
 * production firmware. */
typedef int (*motor_dev_force_fn)(uint8_t mode, int *rc, uint32_t *bits,
                                  uint8_t *before, uint8_t *after);
void motor_dev_set_force(motor_dev_force_fn fn);

/* Handles lines starting with "MOTOR"; returns NULL for anything else.
 * The returned string is a complete reply including CRLF. */
const char *motor_dev_cmd(const char *line);

#endif /* MOTOR_DEV_H */
