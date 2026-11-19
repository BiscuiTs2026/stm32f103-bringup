/*
 * pm_keypad_stm32f1.c - board key/LED pin map for the keypad component.
 *
 * The table lives in the port layer because it is board data (BOARD_GPIO_*
 * constants); the entry layer only consumes the pointer. Split step S8b.
 */
#include "pm_keypad.h"

#include "board_f103c8.h"

static const pm_keypad_map_t s_key_map[] = {
    { .key = { .id = BOARD_GPIO_KEY1 }, .led = { .id = BOARD_GPIO_LED_GREEN }, .name = "KEY1" },
    { .key = { .id = BOARD_GPIO_KEY2 }, .led = { .id = BOARD_GPIO_LED_RED   }, .name = "KEY2" },
};

const pm_keypad_map_t *pm_keypad_map(size_t *n)
{
    if (n != NULL) {
        *n = sizeof(s_key_map) / sizeof(s_key_map[0]);
    }
    return s_key_map;
}
