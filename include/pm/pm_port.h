/*
 * pm_port.h - the port registry: the single seam between the model and a chip.
 *
 * Contract:
 *  - There is exactly one implementation of pm_port_get() in a build, chosen
 *    at link time (stm32f1 backend, or the host mock).
 *  - Operations are const tables; no allocation happens at runtime.
 *  - `ctx` is owned by the port and is never dereferenced by upper layers.
 *  - Vendor types (GPIO_TypeDef, UART_HandleTypeDef, ...) must not appear in
 *    this header or in any header above it.
 */
#ifndef PM_PORT_H
#define PM_PORT_H

#include "pm_common.h"
#include "pm_err.h"

/* ------------------------------------------------------------------ GPIO */

typedef enum {
    PM_GPIO_DIR_IN = 0,
    PM_GPIO_DIR_OUT = 1
} pm_gpio_dir_t;

typedef enum {
    PM_GPIO_PULL_NONE = 0,
    PM_GPIO_PULL_UP,
    PM_GPIO_PULL_DOWN
} pm_gpio_pull_t;

typedef enum {
    PM_GPIO_LEVEL_LOW = 0,
    PM_GPIO_LEVEL_HIGH = 1
} pm_gpio_level_t;

typedef enum {
    PM_GPIO_SPEED_LOW = 0,
    PM_GPIO_SPEED_MEDIUM,
    PM_GPIO_SPEED_HIGH
} pm_gpio_speed_t;

/* A logical pin, not a hardware port/pin pair. The board maps id -> hardware. */
typedef struct {
    uint16_t id;
} pm_gpio_pin_t;

typedef struct {
    pm_gpio_dir_t   dir;
    pm_gpio_pull_t  pull;
    pm_gpio_speed_t speed;
    pm_gpio_level_t initial_level;   /* applied on configure when dir == OUT */
} pm_gpio_cfg_t;

typedef struct {
    pm_err_t (*configure)(void *ctx, pm_gpio_pin_t pin, const pm_gpio_cfg_t *cfg);
    pm_err_t (*write)(void *ctx, pm_gpio_pin_t pin, pm_gpio_level_t level);
    pm_err_t (*read)(void *ctx, pm_gpio_pin_t pin, pm_gpio_level_t *level);
} pm_gpio_ops_t;

/* ------------------------------------------------------------------ UART */

typedef enum {
    PM_UART_PARITY_NONE = 0,
    PM_UART_PARITY_EVEN,
    PM_UART_PARITY_ODD
} pm_uart_parity_t;

typedef enum {
    PM_UART_STOP_1 = 0,
    PM_UART_STOP_2
} pm_uart_stop_t;

typedef struct {
    uint8_t hw_id;            /* logical UART instance, board-defined */
    uint32_t baudrate;
    pm_uart_parity_t parity;
    pm_uart_stop_t   stop_bits;
    uint8_t          data_bits;   /* 8 only in this release */

    uint8_t  *rx_storage;     /* caller-owned, static */
    uint16_t  rx_size;        /* power of two, >= 32 */
    uint8_t  *tx_storage;     /* caller-owned, static */
    uint16_t  tx_size;        /* power of two, >= 64 */
} pm_uart_cfg_t;

typedef struct {
    uint32_t rx_bytes;      /* total bytes received */
    uint32_t tx_bytes;      /* total bytes transmitted */
    uint32_t rx_dropped;    /* bytes lost because the RX ring was full */
    uint32_t tx_dropped;    /* bytes rejected by pm_uart_write */
    uint32_t hw_errors;     /* overrun / framing / noise / parity events */
    uint16_t last_hw_error; /* backend-specific bit mask, 0 if none */
} pm_uart_stats_t;

typedef struct pm_uart pm_uart_t;

typedef struct {
    /* Down-calls: model -> port. All are called with interrupts enabled. */
    pm_err_t (*open)(void *ctx, pm_uart_t *u, const pm_uart_cfg_t *cfg);
    void     (*close)(void *ctx, pm_uart_t *u);
    pm_err_t (*start_tx)(void *ctx, pm_uart_t *u, const uint8_t *data, uint16_t len);
    void     (*enable_rx)(void *ctx, pm_uart_t *u);
    void     (*disable_rx)(void *ctx, pm_uart_t *u);
} pm_uart_ops_t;

/* ----------------------------------------------------------- port registry */

typedef struct {
    const pm_gpio_ops_t *gpio;
    void                *gpio_ctx;
    uint16_t             gpio_pin_count;   /* valid ids are [0, count) */

    const pm_uart_ops_t *uart;
    void                *uart_ctx;
    uint8_t              uart_count;       /* valid hw_id are [0, count) */
} pm_port_t;

const pm_port_t *pm_port_get(void);

#endif /* PM_PORT_H */
