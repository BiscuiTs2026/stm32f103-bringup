/*
 * board_f103c8.h - board description for the KEYSKING STM32F103C8T6 board.
 *
 * This is the ONLY file that has to change when the same library is moved to
 * another board. Pin numbers come from HARDWARE.md section 3.
 */
#ifndef BOARD_F103C8_H
#define BOARD_F103C8_H

/* Logical GPIO ids. Order defines the id used by pm_gpio_*(). */
typedef enum {
    BOARD_GPIO_LED_BLUE = 0,   /* PA6 */
    BOARD_GPIO_LED_GREEN,      /* PA7 */
    BOARD_GPIO_LED_RED,        /* PB0 */
    BOARD_GPIO_KEY1,           /* PB12, external 10k pull-up */
    BOARD_GPIO_KEY2,           /* PB13, no external pull-up */
    BOARD_GPIO_COUNT
} board_gpio_id_t;

/* Logical UART ids. USART1=0, USART2=1, USART3=2. */
typedef enum {
    BOARD_UART_DEBUG = 1,      /* USART2 on PA2/PA3, onboard CH343P */
    BOARD_UART_BT    = 2,      /* USART3 on PB10/PB11, BLUETOOTH header */
    BOARD_UART_COUNT = 3
} board_uart_id_t;

#endif /* BOARD_F103C8_H */
