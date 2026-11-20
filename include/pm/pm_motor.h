/*
 * pm_motor.h - brushed DC motor behind a DRV8833-class dual H-bridge.
 *
 * DEVICE SHAPE (read this first)
 * ---------------------------------------------------------------------------
 * This model targets the DRV8833 shape: TWO input pins per bridge, both of
 * them PWM inputs, no enable pin.
 *
 *   IN1 IN2   OUT       meaning
 *    0   0    Z Z       coast   (fast decay)
 *    1   0    H L       forward
 *    0   1    L H       reverse
 *    1   1    L L       brake   (slow decay, both low-side FETs on)
 *
 * It does NOT fit L298N-style wiring (ENA + IN1 + IN2). There the duty goes
 * on the shared enable pin and direction lives on two plain GPIOs; reversing
 * a DRV8833 by contrast moves the duty onto the *other* input pin, which a
 * GPIO cannot produce. Do not use this model with an L298N.
 *
 * Contract (docs/00-contract.md section 23):
 *  - Both channels must run at the same frequency. init() compares them and
 *    returns PM_ERR_CONFLICT otherwise: two channels of one timer with
 *    different PSC/ARR is the same class of bug as the TIM2 collision.
 *  - set(duty, FWD) drives IN1 with `duty` and holds IN2 low; REV is the
 *    mirror image. `invert` swaps the two.
 *  - set(0, any dir) IS a coast and lands in PM_MOTOR_STATE_STOP_COAST.
 *    It is electrically identical to stop(COAST) and NOT to stop(BRAKE).
 *  - stop(BRAKE) drives both inputs to 1000 permille, i.e. "approximately
 *    constant high" (pm_pwm loses one tick at the top). It is not a cut of
 *    the PWM output; it is a 100% duty request.
 *  - Any set() while braked leaves the brake immediately; duty is never left
 *    sitting at 1000.
 *  - Only fast decay (PWM on one input, the other low) is supported. Slow
 *    decay (PWM on one input, the other high) is deliberately not offered.
 *  - `invert` defaults to "no swap": PM_MOTOR_INVERT_UNSET (the
 *    zero-initialised value) means OFF. Any value outside UNSET/OFF/ON is
 *    rejected with PM_ERR_PARAM.
 */
#ifndef PM_MOTOR_H
#define PM_MOTOR_H

#include "pm_common.h"
#include "pm_err.h"
#include "pm_pwm.h"
#include <stdbool.h>

typedef enum {
    PM_MOTOR_INVERT_UNSET = 0,   /* default: no swap (same as OFF) */
    PM_MOTOR_INVERT_OFF   = 1,
    PM_MOTOR_INVERT_ON    = 2
} pm_motor_invert_t;

typedef enum {
    PM_MOTOR_DIR_FORWARD = 0,
    PM_MOTOR_DIR_REVERSE = 1
} pm_motor_dir_t;

typedef enum {
    PM_MOTOR_STOP_COAST = 0,
    PM_MOTOR_STOP_BRAKE = 1
} pm_motor_stop_t;

typedef enum {
    PM_MOTOR_STATE_STOP_COAST = 0,
    PM_MOTOR_STATE_STOP_BRAKE = 1,
    PM_MOTOR_STATE_RUN        = 2
} pm_motor_state_t;

typedef struct {
    pm_pwm_t         *in1;      /* PWM channel wired to DRV8833 IN1 */
    pm_pwm_t         *in2;      /* PWM channel wired to DRV8833 IN2 */
    pm_motor_invert_t invert;   /* swaps the IN1/IN2 roles */
} pm_motor_cfg_t;

typedef struct pm_motor pm_motor_t;

struct pm_motor {
    uint32_t magic;
    uint8_t  state;
    uint8_t  run_state;
    uint8_t  invert;
    uint8_t  dir;

    uint16_t duty_permille;

    pm_pwm_t *in1;
    pm_pwm_t *in2;
};

/* Both channels must already be open and share one frequency. Returns
 * PM_ERR_PARAM for an invert outside UNSET/OFF/ON or identical channel
 * pointers, PM_ERR_STATE for a closed channel, PM_ERR_CONFLICT for a
 * frequency mismatch, PM_ERR_BUSY when the object is already open. */
pm_err_t pm_motor_init(pm_motor_t *m, const pm_motor_cfg_t *cfg);

/* Coasts the bridge and releases the model. Idempotent. */
pm_err_t pm_motor_deinit(pm_motor_t *m);

/* duty in permille (0..1000); duty 0 is a coast whatever `dir` says. */
pm_err_t pm_motor_set(pm_motor_t *m, uint16_t duty_permille, pm_motor_dir_t dir);

/* COAST = both inputs low (Z, what the board does today).
 * BRAKE = both inputs at 1000 permille (L/L). New capability, not the
 * current board behaviour. */
pm_err_t pm_motor_stop(pm_motor_t *m, pm_motor_stop_t mode);

uint16_t         pm_motor_duty_permille(const pm_motor_t *m);
pm_motor_dir_t   pm_motor_dir(const pm_motor_t *m);
pm_motor_state_t pm_motor_state(const pm_motor_t *m);

/* True only in PM_MOTOR_STATE_RUN. False in coast and in brake. */
bool pm_motor_is_running(const pm_motor_t *m);

#endif /* PM_MOTOR_H */
