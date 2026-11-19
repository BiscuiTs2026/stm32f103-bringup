/*
 * pm_motor.h - DRV8833 motor drive on PA0/PA1 (TIM2_CH1/CH2).
 *
 * The schematic names the two module inputs PWM1 = PA0 (header pin 4) and
 * PWM2 = PA1 (pin 3); the PCB silkscreen prints A0/A1. Both are plain
 * direction inputs on the DRV8833, so the drive scheme is:
 *   forward: CH1 = PWM, CH2 = 0
 *   reverse: CH1 = 0,   CH2 = PWM
 *   stop:    both 0 (coast)
 * Which shaft direction "forward" actually is must be checked on the board.
 */
#ifndef PM_MOTOR_H
#define PM_MOTOR_H

#include <stdint.h>

#define PM_MOTOR_FREQ_HZ 20000u

typedef enum {
    PM_MOTOR_STOP = 0,
    PM_MOTOR_FWD  = 1,
    PM_MOTOR_REV  = 2
} pm_motor_dir_t;

typedef struct {
    uint32_t       freq_hz;   /* derived from the live PSC/ARR pair */
    uint32_t       arr;
    uint16_t       psc;
    uint8_t        duty_pct;  /* last requested duty */
    pm_motor_dir_t dir;       /* last requested direction */
    uint32_t       ccr1;      /* read back from TIM2->CCR1 */
    uint32_t       ccr2;      /* read back from TIM2->CCR2 */
} pm_motor_stat_t;

/* 20 kHz PWM on CH1/CH2, both channels low. Returns 0, -1 if TIM2 is already
 * counting (another owner), -2 if CH1/CH2 are remapped off PA0/PA1. */
int pm_motor_init(void);

/* Direction plus duty 0..100 (100 = CCR above ARR, output always active).
 * PM_MOTOR_STOP forces both channels low. Returns -1 on a bad argument. */
int pm_motor_set(pm_motor_dir_t dir, uint8_t duty_pct);

/* Change duty and keep the current direction. Returns -1 on a bad value or
 * when no direction has been commanded yet. */
int pm_motor_duty(uint8_t duty_pct);

void pm_motor_stop(void);

/* Snapshot of the timer registers plus the requested state. */
void pm_motor_stat(pm_motor_stat_t *out);

#endif /* PM_MOTOR_H */
