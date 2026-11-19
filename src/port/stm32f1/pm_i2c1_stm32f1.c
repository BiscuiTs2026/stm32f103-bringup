/*
 * pm_i2c1_stm32f1.c - polling I2C1 master for the AHT20 and the CH1116 OLED.
 *
 * Moved out of src/app/main.c unchanged in behaviour (split step S4): PB6 =
 * SCL, PB7 = SDA, 100 kHz from APB1 = 36 MHz. Register access has to live in
 * the bsp layer. Reported addresses are 8-bit write addresses (7-bit << 1),
 * matching the OLED 0x7A / AHT20 0x70 convention.
 *
 * Three bus re-initialisations are deliberate and must never be merged into a
 * single power-up init: the entry of i2c1_scan(), the entry of aht_trigger()
 * and oled_bus_write() before the retry after a failed wait. They are the bus
 * self-healing path; merging them still compiles but leaves the bus wedged
 * after a fault.
 */
#include "pm_i2c1.h"

#include "pm_common.h"

#include "stm32f1xx.h"

#define I2C_SPIN 200000u
#define I2C_FRAME_SPIN 50000u
#define I2C_CH1116_ADDR8 0x7Au   /* same address as OLED_CH1116_ADDR8 */

/* 1 ms steps off the SysTick down-counter (armed by pm_platform_init). The bsp
 * layer may not call pm_osal_delay_ms (layering rule 17). */
static void i2c1_delay_ms(uint32_t ms)
{
    while (ms-- != 0u) {
        (void)SysTick->CTRL;                     /* read clears COUNTFLAG */
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0u) {
        }
    }
}

static int i2c1_wait_sr1(uint32_t mask)
{
    uint32_t t = I2C_SPIN;

    while (t-- != 0u) {
        uint32_t sr1 = I2C1->SR1;

        if ((sr1 & mask) != 0u) {
            return 1;
        }
        if ((sr1 & (I2C_SR1_AF | I2C_SR1_BERR)) != 0u) {
            return 0;
        }
    }
    return 0;
}

static void i2c1_bus_fix(void)
{
    /* Release a slave that holds SDA low: 9 clocks on SCL, then STOP. */
    uint32_t i;

    I2C1->CR1 &= ~I2C_CR1_PE;
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    /* PB6/PB7 back to GPIO open-drain while clocking. */
    GPIOB->CRL = (GPIOB->CRL & ~((0xFu << 24) | (0xFu << 28)))
               | (0x7u << 24) | (0x7u << 28);
    for (i = 0u; i < 9u; i++) {
        GPIOB->BRR = (1u << 6);
        i2c1_delay_ms(1u);
        GPIOB->BSRR = (1u << 6);
        i2c1_delay_ms(1u);
    }
    GPIOB->BRR = (1u << 6);
    i2c1_delay_ms(1u);
    GPIOB->BSRR = (1u << 6);
    i2c1_delay_ms(1u);

    I2C1->CR1 = 0u;
    I2C1->CR2 = 36u;
    I2C1->CCR = 180u;
    I2C1->TRISE = 37u;
    I2C1->CR1 = I2C_CR1_PE;
}

static void i2c1_bus_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    GPIOB->CRL = (GPIOB->CRL & ~((0xFu << 24) | (0xFu << 28)))
               | (0xFu << 24) | (0xFu << 28);      /* PB6/PB7 AF open-drain */

    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0u;
    I2C1->CR2 = 36u;                                /* APB1 = 36 MHz */
    I2C1->CCR = 180u;                               /* 100 kHz */
    I2C1->TRISE = 37u;
    I2C1->CR1 = I2C_CR1_PE;
}

static size_t i2c1_scan(uint8_t *found, size_t max)
{
    size_t   n = 0u;
    uint16_t addr;

    i2c1_bus_init();

    for (addr = 0x08u; addr <= 0x77u; addr++) {
        I2C1->CR1 |= I2C_CR1_START;
        if (i2c1_wait_sr1(I2C_SR1_SB) == 0) {
            i2c1_bus_fix();
            continue;
        }
        (void)I2C1->SR2;                            /* clear SB */
        I2C1->DR = (uint8_t)(addr << 1);
        if (i2c1_wait_sr1(I2C_SR1_ADDR | I2C_SR1_AF) != 0) {
            if ((I2C1->SR1 & I2C_SR1_ADDR) != 0u) {
                (void)I2C1->SR2;
                if (n < max) {
                    found[n] = (uint8_t)(addr << 1);
                    n++;
                }
            } else {
                I2C1->SR1 &= ~I2C_SR1_AF;
            }
        }
        I2C1->CR1 |= I2C_CR1_STOP;
        {
            uint32_t t = I2C_SPIN;

            while (((I2C1->CR1 & I2C_CR1_STOP) != 0u) && (t-- != 0u)) {
            }
        }
    }
    I2C1->CR1 &= ~I2C_CR1_PE;
    return n;
}

/* Read the result bytes after the conversion window. aht_trigger() already
 * ended the command with STOP, so this is a plain START on a released bus;
 * there is no repeated-START path any more. */
static int i2c1_read_after_trigger(uint8_t addr8r, uint8_t *rd, size_t rn)
{
    size_t i;

    I2C1->CR1 |= I2C_CR1_START;                     /* START, bus released */
    if (i2c1_wait_sr1(I2C_SR1_SB) == 0) {
        return 0;
    }
    (void)I2C1->SR2;
    I2C1->DR = addr8r;
    I2C1->CR1 |= I2C_CR1_ACK;
    if (i2c1_wait_sr1(I2C_SR1_ADDR) == 0) {
        if ((I2C1->SR1 & I2C_SR1_AF) != 0u) {
            I2C1->SR1 &= ~I2C_SR1_AF;
        }
        return 0;
    }
    (void)I2C1->SR2;
    for (i = 0u; i < rn; i++) {
        if (i == (rn - 1u)) {
            I2C1->CR1 &= ~I2C_CR1_ACK;
        }
        if (i2c1_wait_sr1(I2C_SR1_RXNE) == 0) {
            return 0;
        }
        rd[i] = (uint8_t)I2C1->DR;
    }
    I2C1->CR1 |= I2C_CR1_STOP;
    return 1;
}

static void i2c1_stop_and_wait(void)
{
    uint32_t t = I2C_SPIN;

    I2C1->CR1 |= I2C_CR1_STOP;
    while (((I2C1->CR1 & I2C_CR1_STOP) != 0u) && (t-- != 0u)) {
    }
}

static int aht20_trigger(void)
{
    static const uint8_t trig[3] = { 0xACu, 0x33u, 0x00u };
    size_t i;

    I2C1->CR1 |= I2C_CR1_START;
    if (i2c1_wait_sr1(I2C_SR1_SB) == 0) {
        return 0;
    }
    (void)I2C1->SR2;
    I2C1->DR = 0x70u;
    if (i2c1_wait_sr1(I2C_SR1_ADDR) == 0) {
        return 0;
    }
    (void)I2C1->SR2;
    for (i = 0u; i < 3u; i++) {
        if (i2c1_wait_sr1(I2C_SR1_TXE) == 0) {
            return 0;
        }
        I2C1->DR = trig[i];
    }
    return i2c1_wait_sr1(I2C_SR1_BTF);
}

/* Trigger one conversion and end the command with STOP. The AHT20 starts
 * converting when the command is terminated, so the STOP has to be issued
 * before the 80 ms window; delaying it made the status byte read back 0x98
 * (busy). That means AHTS reports rst=1 but releases the bus 80 ms earlier
 * than the old glue did: its read is a plain START instead of a repeated one,
 * which the sensor accepts (same as the AHT path). */
static int aht_trigger(void)
{
    i2c1_bus_init();
    if (aht20_trigger() == 0) {
        i2c1_stop_and_wait();
        I2C1->CR1 &= ~I2C_CR1_PE;
        return 0;
    }
    i2c1_stop_and_wait();
    return 1;
}

/* Both modes land here after aht_trigger() released the bus with STOP, so the
 * read below is a plain START. `repeated_start` stays in the signature only
 * because pm_i2c1_ops_t is the fixed common contract (SPLIT-PLAN §2.2); it is
 * not consulted on the wire and must not be mistaken for a live branch. */
static int aht_read(uint8_t *raw, size_t n, int repeated_start)
{
    int ok;

    PM_UNUSED(repeated_start);

    ok = i2c1_read_after_trigger(0x71u, raw, n);
    if (ok == 0) {
        i2c1_stop_and_wait();
    }
    I2C1->CR1 &= ~I2C_CR1_PE;
    return ok;
}

static uint32_t s_oled_af;
static uint32_t s_oled_berr;
static uint32_t s_oled_to;

static int oled_i2c_wait(uint32_t mask)
{
    uint32_t t = I2C_FRAME_SPIN;

    while (t-- != 0u) {
        uint32_t sr1 = I2C1->SR1;

        if ((sr1 & mask) != 0u) {
            return 0;
        }
        if ((sr1 & I2C_SR1_AF) != 0u) {
            I2C1->SR1 &= ~I2C_SR1_AF;
            s_oled_af++;
            return -1;
        }
        if ((sr1 & I2C_SR1_BERR) != 0u) {
            I2C1->SR1 &= ~I2C_SR1_BERR;
            s_oled_berr++;
            return -2;
        }
    }
    s_oled_to++;
    return -3;
}

/* One I2C frame: START, 0x7A, control byte, payload, STOP. */
static int oled_bus_write(void *ctx, uint8_t control, const uint8_t *d,
                          uint16_t n)
{
    uint16_t i;
    int      rc;

    PM_UNUSED(ctx);

    if (((RCC->APB1ENR & RCC_APB1ENR_I2C1EN) == 0u) ||
        ((I2C1->CR1 & I2C_CR1_PE) == 0u)) {
        i2c1_bus_init();
    }

    I2C1->CR1 |= I2C_CR1_START;
    rc = oled_i2c_wait(I2C_SR1_SB);
    if (rc == 0) {
        (void)I2C1->SR2;                    /* clear SB */
        I2C1->DR = I2C_CH1116_ADDR8;
        rc = oled_i2c_wait(I2C_SR1_ADDR);
    }
    if (rc == 0) {
        (void)I2C1->SR2;                    /* clear ADDR, slave ACKed */
        rc = oled_i2c_wait(I2C_SR1_TXE);
    }
    if (rc == 0) {
        I2C1->DR = control;
        for (i = 0u; (i < n) && (rc == 0); i++) {
            rc = oled_i2c_wait(I2C_SR1_TXE);
            if (rc == 0) {
                I2C1->DR = d[i];
            }
        }
    }
    if (rc == 0) {
        rc = oled_i2c_wait(I2C_SR1_BTF);
    }
    i2c1_stop_and_wait();
    return rc;
}

static const pm_i2c1_ops_t s_ops = {
    .init        = i2c1_bus_init,
    .scan        = i2c1_scan,
    .aht_trigger = aht_trigger,
    .aht_read    = aht_read,
    .write       = oled_bus_write,
};

const pm_i2c1_ops_t *pm_i2c1_ops(void)
{
    return &s_ops;
}

void pm_i2c1_stats(pm_i2c1_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->af   = s_oled_af;
    out->berr = s_oled_berr;
    out->to   = s_oled_to;
}
