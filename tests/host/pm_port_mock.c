/*
 * pm_port_mock.c - a fully in-memory port used by the host tests.
 *
 * It proves the contract claim "the model does not need hardware": the exact
 * same pm_core sources are compiled for the host against this file.
 *
 * TX behaviour: start_tx() copies the chunk into a capture buffer and returns
 * immediately. The test decides when to call pm_uart_hw_tx_complete(), which
 * is what the real TX interrupt would do.
 */
#include "pm_port_mock.h"
#include "pm_uart.h"
#include <string.h>

static pm_gpio_level_t s_level[MOCK_GPIO_COUNT];
static uint8_t         s_configured[MOCK_GPIO_COUNT];

static uint8_t s_uart_open[MOCK_UART_COUNT];
static uint8_t s_tx_capture[MOCK_TX_CAPTURE_SIZE];
static size_t  s_tx_len;

/* ------------------------------------------------------------------ GPIO */

static pm_err_t mock_gpio_configure(void *ctx, pm_gpio_pin_t pin,
                                    const pm_gpio_cfg_t *cfg)
{
    (void)ctx;
    if (cfg == NULL) {
        return PM_ERR_NULL;
    }
    if (pin.id >= MOCK_GPIO_COUNT) {
        return PM_ERR_RANGE;
    }
    s_configured[pin.id] = 1u;
    s_level[pin.id] = cfg->initial_level;
    return PM_OK;
}

static pm_err_t mock_gpio_write(void *ctx, pm_gpio_pin_t pin, pm_gpio_level_t level)
{
    (void)ctx;
    if (pin.id >= MOCK_GPIO_COUNT) {
        return PM_ERR_RANGE;
    }
    s_level[pin.id] = level;
    return PM_OK;
}

static pm_err_t mock_gpio_read(void *ctx, pm_gpio_pin_t pin, pm_gpio_level_t *level)
{
    (void)ctx;
    if ((pin.id >= MOCK_GPIO_COUNT) || (level == NULL)) {
        return PM_ERR_RANGE;
    }
    *level = s_level[pin.id];
    return PM_OK;
}

static const pm_gpio_ops_t s_gpio_ops = {
    .configure = mock_gpio_configure,
    .write     = mock_gpio_write,
    .read      = mock_gpio_read,
};

/* ------------------------------------------------------------------ UART */

static pm_err_t mock_uart_open(void *ctx, pm_uart_t *u, const pm_uart_cfg_t *cfg)
{
    (void)ctx;
    (void)u;
    if (cfg->hw_id >= MOCK_UART_COUNT) {
        return PM_ERR_RANGE;
    }
    if (s_uart_open[cfg->hw_id] != 0u) {
        return PM_ERR_BUSY;
    }
    s_uart_open[cfg->hw_id] = 1u;
    return PM_OK;
}

static void mock_uart_close(void *ctx, pm_uart_t *u)
{
    (void)ctx;
    if (u->hw_id < MOCK_UART_COUNT) {
        s_uart_open[u->hw_id] = 0u;
    }
}

static pm_err_t mock_uart_start_tx(void *ctx, pm_uart_t *u,
                                   const uint8_t *data, uint16_t len)
{
    (void)ctx;
    (void)u;

    if ((data == NULL) || (len == 0u)) {
        return PM_ERR_PARAM;
    }
    if ((s_tx_len + len) > MOCK_TX_CAPTURE_SIZE) {
        return PM_ERR_NOMEM;
    }
    memcpy(&s_tx_capture[s_tx_len], data, len);
    s_tx_len += len;
    return PM_OK;
}

static void mock_uart_enable_rx(void *ctx, pm_uart_t *u)
{
    (void)ctx;
    (void)u;
}

static void mock_uart_disable_rx(void *ctx, pm_uart_t *u)
{
    (void)ctx;
    (void)u;
}

static const pm_uart_ops_t s_uart_ops = {
    .open       = mock_uart_open,
    .close      = mock_uart_close,
    .start_tx   = mock_uart_start_tx,
    .enable_rx  = mock_uart_enable_rx,
    .disable_rx = mock_uart_disable_rx,
};

/* ---------------------------------------------------------- port registry */

static const pm_port_t s_port = {
    .gpio           = &s_gpio_ops,
    .gpio_ctx       = NULL,
    .gpio_pin_count = (uint16_t)MOCK_GPIO_COUNT,
    .uart           = &s_uart_ops,
    .uart_ctx       = NULL,
    .uart_count     = (uint8_t)MOCK_UART_COUNT,
};

const pm_port_t *pm_port_get(void)
{
    return &s_port;
}

/* ------------------------------------------------------------ test controls */

void mock_reset(void)
{
    memset(s_level, 0, sizeof(s_level));
    memset(s_configured, 0, sizeof(s_configured));
    memset(s_uart_open, 0, sizeof(s_uart_open));
    memset(s_tx_capture, 0, sizeof(s_tx_capture));
    s_tx_len = 0u;
}

int mock_gpio_is_configured(uint16_t id)
{
    return (id < MOCK_GPIO_COUNT) ? (int)s_configured[id] : 0;
}

pm_gpio_level_t mock_gpio_level(uint16_t id)
{
    return (id < MOCK_GPIO_COUNT) ? s_level[id] : PM_GPIO_LEVEL_LOW;
}

int mock_uart_is_open(uint8_t hw_id)
{
    return (hw_id < MOCK_UART_COUNT) ? (int)s_uart_open[hw_id] : 0;
}

size_t mock_uart_tx_len(void)
{
    return s_tx_len;
}

const uint8_t *mock_uart_tx_data(void)
{
    return s_tx_capture;
}
