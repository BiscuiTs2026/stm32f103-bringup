/*
 * pm_gpio.h - GPIO model API.
 *
 * GPIO is synchronous: no ISR, no queue, no event.
 *
 * Rules:
 *  - Ids are board-defined logical pins (pm_board_gpio_id_t in the board
 *    header). Upper layers never see GPIOA / GPIO_PIN_x.
 *  - Every call validates the id against the port table and returns
 *    PM_ERR_RANGE instead of touching an unknown pin.
 *  - pm_gpio_read() writes PM_GPIO_LEVEL_LOW into *level on error, so the
 *    output is always defined.
 */
#ifndef PM_GPIO_H
#define PM_GPIO_H

#include "pm_common.h"
#include "pm_err.h"
#include "pm_port.h"

pm_err_t pm_gpio_configure(pm_gpio_pin_t pin, const pm_gpio_cfg_t *cfg);
pm_err_t pm_gpio_write(pm_gpio_pin_t pin, pm_gpio_level_t level);
pm_err_t pm_gpio_read(pm_gpio_pin_t pin, pm_gpio_level_t *level);
pm_err_t pm_gpio_toggle(pm_gpio_pin_t pin);

#endif /* PM_GPIO_H */
