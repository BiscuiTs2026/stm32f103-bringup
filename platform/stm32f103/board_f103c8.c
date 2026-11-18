/*
 * board_f103c8.c - board pin table for the KEYSKING STM32F103C8T6 board.
 *
 * Only the port layer sees GPIOA/GPIOB. Upper layers use board_gpio_id_t.
 */
#include "board_f103c8.h"
#include "pm_port_stm32f1.h"

static const pm_f1_gpio_map_t s_gpio_map[BOARD_GPIO_COUNT] = {
    [BOARD_GPIO_LED_BLUE]  = { GPIOA, GPIO_PIN_6 },
    [BOARD_GPIO_LED_GREEN] = { GPIOA, GPIO_PIN_7 },
    [BOARD_GPIO_LED_RED]   = { GPIOB, GPIO_PIN_0 },
    [BOARD_GPIO_KEY1]      = { GPIOB, GPIO_PIN_12 },
    [BOARD_GPIO_KEY2]      = { GPIOB, GPIO_PIN_13 },
};

const pm_f1_gpio_ctx_t pm_f1_gpio_ctx = {
    .map   = s_gpio_map,
    .count = (uint16_t)BOARD_GPIO_COUNT,
};
