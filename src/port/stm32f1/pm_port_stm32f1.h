/*
 * pm_port_stm32f1.h - internal header of the STM32F1 port.
 *
 * Vendor types are allowed here and only here (plus the .c files of this
 * directory). Nothing above the port may include this header.
 */
#ifndef PM_PORT_STM32F1_H
#define PM_PORT_STM32F1_H

#include "pm_port.h"
#include "stm32f1xx.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} pm_f1_gpio_map_t;

typedef struct {
    const pm_f1_gpio_map_t *map;
    uint16_t                count;
} pm_f1_gpio_ctx_t;

/* Defined by the board file (platform/<chip>/board_*.c). */
extern const pm_f1_gpio_ctx_t pm_f1_gpio_ctx;

#endif /* PM_PORT_STM32F1_H */
