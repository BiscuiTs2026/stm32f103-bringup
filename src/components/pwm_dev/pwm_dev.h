/*
 * pwm_dev.h - command front end for the servo and the passive buzzer.
 *
 * The glue calls pwm_dev_cmd() with one already-parsed command line. A NULL
 * return means "not mine"; otherwise the returned pointer holds the single
 * line reply (CRLF terminated) that the caller sends verbatim.
 */
#ifndef PWM_DEV_H
#define PWM_DEV_H

/* Brings the two outputs up. Safe to call once from the entry layer. */
void pwm_dev_init(void);

/* Returns NULL when `line` is not a PWM command. */
const char *pwm_dev_cmd(const char *line);

#endif /* PWM_DEV_H */
