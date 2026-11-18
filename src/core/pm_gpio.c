#include "pm_gpio.h"

static const pm_gpio_ops_t *gpio_ops(void)
{
    const pm_port_t *port = pm_port_get();

    if ((port == NULL) || (port->gpio == NULL)) {
        return NULL;
    }
    return port->gpio;
}

static bool pin_in_range(pm_gpio_pin_t pin)
{
    const pm_port_t *port = pm_port_get();

    return (port != NULL) && (pin.id < port->gpio_pin_count);
}

pm_err_t pm_gpio_configure(pm_gpio_pin_t pin, const pm_gpio_cfg_t *cfg)
{
    const pm_gpio_ops_t *ops = gpio_ops();

    if (cfg == NULL) {
        return PM_ERR_NULL;
    }
    if (ops == NULL) {
        return PM_ERR_STATE;
    }
    if (!pin_in_range(pin)) {
        return PM_ERR_RANGE;
    }
    return ops->configure(pm_port_get()->gpio_ctx, pin, cfg);
}

pm_err_t pm_gpio_write(pm_gpio_pin_t pin, pm_gpio_level_t level)
{
    const pm_gpio_ops_t *ops = gpio_ops();

    if (ops == NULL) {
        return PM_ERR_STATE;
    }
    if ((level != PM_GPIO_LEVEL_LOW) && (level != PM_GPIO_LEVEL_HIGH)) {
        return PM_ERR_PARAM;
    }
    if (!pin_in_range(pin)) {
        return PM_ERR_RANGE;
    }
    return ops->write(pm_port_get()->gpio_ctx, pin, level);
}

pm_err_t pm_gpio_read(pm_gpio_pin_t pin, pm_gpio_level_t *level)
{
    const pm_gpio_ops_t *ops = gpio_ops();
    pm_err_t rc;

    if (level == NULL) {
        return PM_ERR_NULL;
    }
    *level = PM_GPIO_LEVEL_LOW;       /* output is always defined */

    if (ops == NULL) {
        return PM_ERR_STATE;
    }
    if (!pin_in_range(pin)) {
        return PM_ERR_RANGE;
    }

    rc = ops->read(pm_port_get()->gpio_ctx, pin, level);
    if (rc != PM_OK) {
        *level = PM_GPIO_LEVEL_LOW;
    }
    return rc;
}

pm_err_t pm_gpio_toggle(pm_gpio_pin_t pin)
{
    pm_gpio_level_t level;
    pm_err_t rc;

    rc = pm_gpio_read(pin, &level);
    if (rc != PM_OK) {
        return rc;
    }
    return pm_gpio_write(pin,
                         (level == PM_GPIO_LEVEL_HIGH) ? PM_GPIO_LEVEL_LOW
                                                       : PM_GPIO_LEVEL_HIGH);
}
