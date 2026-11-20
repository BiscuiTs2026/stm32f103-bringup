/*
 * sonar_dev.h - HC-SR04 measurement job (DIST command).
 *
 * Moved out of src/app/main.c unchanged in behaviour (split step S6b). The
 * pin, EXTI10 and TIM2 time-stamp work stays in
 * src/port/stm32f1/pm_sonar_stm32f1.c; this component owns the non-blocking
 * job, the reply formatting and the last-good value shown on the live page.
 */
#ifndef SONAR_DEV_H
#define SONAR_DEV_H

#include <stdint.h>

#include "pm_cmd_out.h"     /* cmd_port_t */

/* Starts one measurement on port c. auto_live != 0 marks the periodic poke
 * from the live page: when DIST is disabled it stays silent, and its replies
 * carry the "DIST live " prefix. */
void sonar_dev_start(cmd_port_t *c, uint8_t auto_live);

/* Advances the job. Call once per main-loop pass with the same tick the other
 * services use; it never blocks. */
void sonar_dev_service(uint32_t now);

/* 1 while a measurement is in flight (screen hook). */
int sonar_dev_busy(void);

/* Live-page hook: starts one measurement if possible. */
void sonar_dev_live_poke(cmd_port_t *port);

#endif /* SONAR_DEV_H */
