/*
 * pm_i2c1.h - I2C1 polling master contract (AHT20 + CH1116 on PB6/PB7).
 *
 * The implementation lives in src/port/stm32f1/pm_i2c1_stm32f1.c; that bsp
 * file is the only place allowed to touch I2C1/GPIO registers. Callers reach
 * the bus through this table so the polling code can move without touching
 * them again. No string formatting happens behind this interface: `scan`
 * returns addresses only and the caller builds the reply.
 */
#ifndef PM_I2C1_H
#define PM_I2C1_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void   (*init)(void);
    size_t (*scan)(uint8_t *found, size_t max);   /* count, 8-bit write addrs ascending */
    int    (*aht_trigger)(void);
    int    (*aht_read)(uint8_t *raw, size_t n, int repeated_start); /* unused on the wire */
    int    (*write)(void *ctx, uint8_t control, const uint8_t *data, uint16_t len);
} pm_i2c1_ops_t;   /* write matches oled_ch1116_bus_fn */

typedef struct { uint32_t af; uint32_t berr; uint32_t to; } pm_i2c1_stats_t;

const pm_i2c1_ops_t *pm_i2c1_ops(void);
void pm_i2c1_stats(pm_i2c1_stats_t *out);

#endif /* PM_I2C1_H */
