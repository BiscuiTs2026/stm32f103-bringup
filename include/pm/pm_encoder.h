/*
 * pm_encoder.h - incremental quadrature encoder with x4 decoding.
 *
 * The decode table lives in the core and is fed from the port's edge ISR:
 *
 *   AB 00 -> 01 -> 11 -> 10 -> 00  is one direction, +1 per valid edge.
 *   Any diagonal transition (00->11, ...) is ignored: it is either a missed
 *   edge or noise, and counting it would add a wrong step.
 *
 * Contract:
 *  - init/deinit configure the two input pins; counting happens in the ISR
 *    hook pm_encoder_hw_edge(). The core owns the state and the count.
 *  - pm_encoder_count() reads the 32-bit counter; on Cortex-M3 a 32-bit
 *    aligned read is atomic, so no critical section is needed.
 *  - pm_encoder_reset() must be called while the shaft is still, otherwise
 *    one in-flight edge can land after the reset (documented, not hidden).
 *  - `invert` swaps A and B for boards where the encoder is wired backwards.
 *  - The push button is OPTIONAL. `pm_encoder_cfg_t.has_button` defaults to 0
 *    and the model then never touches a button pin: init still succeeds and
 *    pm_encoder_button() answers PM_ERR_DISABLED. Nothing assumes the module
 *    has an SW pin.
 *  - On this board PA8/PA9 are the encoder inputs and PB15 is the EC11 push
 *    button (no external pull-up; the port enables the internal one). PB15's
 *    alternate function is SPI2_MOSI, so button and SPI2 are exclusive.
 */
#ifndef PM_ENCODER_H
#define PM_ENCODER_H

#include "pm_common.h"
#include "pm_err.h"
#include "pm_port.h"

struct pm_encoder {
    uint32_t magic;
    uint8_t  state;
    uint8_t  hw_id;
    uint8_t  invert;
    uint8_t  has_button;       /* 1 when cfg.has_button was non-zero */
    uint8_t  last_ab;          /* last 2-bit state, bit1 = A, bit0 = B */
    volatile int32_t count;    /* updated only from the edge ISR */
    const pm_encoder_ops_t *ops;
    void                   *port_ctx;
};

/* Opens the two pins. Returns PM_ERR_RANGE for an unknown hw_id,
 * PM_ERR_BUSY when the object is already open, and PM_ERR_DISABLED when a
 * button is requested but the port has no read_button() implementation. */
pm_err_t pm_encoder_init(pm_encoder_t *e, const pm_encoder_cfg_t *cfg);

/* Stops counting and releases the pins. Idempotent. */
pm_err_t pm_encoder_deinit(pm_encoder_t *e);

/* Current position in x4 counts. 0 for an uninitialised object. */
int32_t pm_encoder_count(const pm_encoder_t *e);

/* Sets the counter back to 0. Call with the shaft still. */
pm_err_t pm_encoder_reset(pm_encoder_t *e);

/* Last raw AB state (bit1 = A, bit0 = B), 0 when not initialized. */
uint8_t pm_encoder_last_ab(const pm_encoder_t *e);

/* True only when the object is open AND cfg.has_button was non-zero, i.e. the
 * module really has a push button. False for an uninitialised object. */
bool pm_encoder_has_button(const pm_encoder_t *e);

/* Reads the optional push button. true = pressed.
 * PM_ERR_NULL  : e or pressed is NULL
 * PM_ERR_STATE : not initialised
 * PM_ERR_DISABLED : no button on this module; that is an answer, not a fault */
pm_err_t pm_encoder_button(pm_encoder_t *e, bool *pressed);

/* Port -> core hook. Must be called from the pin edge ISR with the new
 * 2-bit state, bit1 = A, bit0 = B. Safe to call when uninitialised. */
void pm_encoder_hw_edge(pm_encoder_t *e, uint8_t ab);

#endif /* PM_ENCODER_H */
