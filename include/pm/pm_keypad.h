/*
 * pm_keypad.h - keypad pin-map contract shared by the port layer and the
 * keypad component.
 *
 * The board pin table itself belongs to the port layer
 * (src/port/stm32f1/pm_keypad_stm32f1.c); the entry layer only fetches the
 * pointer and hands it to keypad_dev_init(). Keeping the type here avoids a
 * port -> component header dependency.
 */
#ifndef PM_KEYPAD_H
#define PM_KEYPAD_H

#include <stddef.h>

#include "pm_gpio.h"

#define KEYPAD_MAX_KEYS 4u

typedef struct {
    pm_gpio_pin_t key;   /* active low */
    pm_gpio_pin_t led;   /* on while pressed */
    const char   *name;
} pm_keypad_map_t;

/* Returns the board table; *n receives the number of entries. */
const pm_keypad_map_t *pm_keypad_map(size_t *n);

#endif /* PM_KEYPAD_H */
