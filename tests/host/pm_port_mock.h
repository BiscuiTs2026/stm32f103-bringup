/*
 * pm_port_mock.h - test controls for the mock port.
 */
#ifndef PM_PORT_MOCK_H
#define PM_PORT_MOCK_H

#include "pm_port.h"

#define MOCK_GPIO_COUNT 4u
#define MOCK_UART_COUNT 2u
#define MOCK_TX_CAPTURE_SIZE 4096u

void            mock_reset(void);

int             mock_gpio_is_configured(uint16_t id);
pm_gpio_level_t mock_gpio_level(uint16_t id);

int             mock_uart_is_open(uint8_t hw_id);
size_t          mock_uart_tx_len(void);
const uint8_t  *mock_uart_tx_data(void);

#endif /* PM_PORT_MOCK_H */
