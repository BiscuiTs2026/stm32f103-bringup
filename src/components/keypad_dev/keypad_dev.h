/*
 * keypad_dev.h - debounced keys with LED mirror and press counters.
 *
 * The pin mapping is injected by the entry layer (pm_keypad_map_t), so this
 * component never includes a board header and never sees a GPIO port name.
 * Keys are active-low with the internal pull-up, as on the KEYSKING board
 * where KEY2 has no external pull-up.
 */
#ifndef KEYPAD_DEV_H
#define KEYPAD_DEV_H

#include <stddef.h>
#include <stdint.h>

#include "pm_cmd_out.h"
#include "pm_gpio.h"
#include "pm_keypad.h"

/* 0 = ok. Configures each key as input with pull-up and each LED as output
 * driven low, in the order given. */
int keypad_dev_init(const pm_keypad_map_t *map, size_t n);

/* 20 ms debounce; counts and reports on the press edge, drives the LED and
 * reports on both edges. */
void keypad_dev_service(uint32_t now);

/* KEYS: replies first, then clears the counters and the last-key tag. */
void keypad_dev_cmd(cmd_port_t *c);

/* Press counters, key 1 and key 2. Keys that were not configured read 0. */
void keypad_dev_counts(uint32_t *k1, uint32_t *k2);

#endif /* KEYPAD_DEV_H */
