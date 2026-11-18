/*
 * main.c - entry glue for the STM32F103C8T6 board bring-up.
 *
 * This file only sequences initialization and schedules work. It contains no
 * driver code and no register access; everything goes through pm_* APIs.
 *
 * Behaviour on the KEYSKING learning board:
 *   - blue LED (PA6) toggles every 500 ms as a heartbeat
 *   - green LED (PA7) mirrors KEY1 (PB12), red LED (PB0) mirrors KEY2 (PB13)
 *   - KEY edges are debounced for 20 ms and reported on the UART
 *   - USART2 (PA2/PA3, 115200) and USART3 (PB10/PB11, 9600) share one command
 *     parser: HELP / PING / STAT / I2C / KEYS
 *   - "hb\r\n" is sent once per second on USART2
 */
#include "pm_common.h"
#include "pm_osal.h"
#include "pm_gpio.h"
#include "pm_uart.h"
#include "pm_platform.h"
#include "board_f103c8.h"
#include "oled_ch1116.h"

#include "stm32f1xx.h"     /* bring-up only: EXTI/DWT pulse measurement */

#include <string.h>

#define KEY_DEBOUNCE_MS 20u

static pm_uart_t s_uart;
static uint8_t   s_rx_storage[256];
static uint8_t   s_tx_storage[512];

static uint32_t s_uart_errors;

static const pm_gpio_cfg_t s_led_cfg = {
    .dir           = PM_GPIO_DIR_OUT,
    .pull          = PM_GPIO_PULL_NONE,
    .speed         = PM_GPIO_SPEED_LOW,
    .initial_level = PM_GPIO_LEVEL_LOW,
};

/* Keys are active-low. KEY2 has no external pull-up on this board, so the
 * internal pull-up is mandatory for both. */
static const pm_gpio_cfg_t s_key_cfg = {
    .dir           = PM_GPIO_DIR_IN,
    .pull          = PM_GPIO_PULL_UP,
    .speed         = PM_GPIO_SPEED_LOW,
    .initial_level = PM_GPIO_LEVEL_HIGH,
};

static const pm_uart_cfg_t s_uart_cfg = {
    .hw_id      = BOARD_UART_DEBUG,
    .baudrate   = 115200u,
    .parity     = PM_UART_PARITY_NONE,
    .stop_bits  = PM_UART_STOP_1,
    .data_bits  = 8u,
    .rx_storage = s_rx_storage,
    .rx_size    = (uint16_t)sizeof(s_rx_storage),
    .tx_storage = s_tx_storage,
    .tx_size    = (uint16_t)sizeof(s_tx_storage),
};

/* Bluetooth module on USART3 (PB10/PB11): 9600 8N1, shared command parser. */
static pm_uart_t s_bt;
static uint8_t   s_bt_rx_storage[128];
static uint8_t   s_bt_tx_storage[512];

static const pm_uart_cfg_t s_bt_cfg = {
    .hw_id      = BOARD_UART_BT,
    .baudrate   = 9600u,            /* DX-BT24 factory default */
    .parity     = PM_UART_PARITY_NONE,
    .stop_bits  = PM_UART_STOP_1,
    .data_bits  = 8u,
    .rx_storage = s_bt_rx_storage,
    .rx_size    = (uint16_t)sizeof(s_bt_rx_storage),
    .tx_storage = s_bt_tx_storage,
    .tx_size    = (uint16_t)sizeof(s_bt_tx_storage),
};

typedef struct {
    pm_gpio_pin_t pin;
    pm_gpio_pin_t led;
    const char   *name;
    uint8_t       stable;      /* 1 = pressed */
    uint8_t       raw;
    uint32_t      raw_since;
} key_state_t;

static key_state_t s_keys[] = {
    { .pin = { .id = BOARD_GPIO_KEY1 }, .led = { .id = BOARD_GPIO_LED_GREEN }, .name = "KEY1" },
    { .pin = { .id = BOARD_GPIO_KEY2 }, .led = { .id = BOARD_GPIO_LED_RED   }, .name = "KEY2" },
};

#define KEY_COUNT ((size_t)(sizeof(s_keys) / sizeof(s_keys[0])))

/* Command channel state; the parser itself lives further down. */
#define CMD_LINE_MAX 48u

typedef struct {
    pm_uart_t *port;
    char       line[CMD_LINE_MAX];
    size_t     len;
    uint32_t   last_ms;    /* last byte seen on this port, for idle flush */
} cmd_port_t;

static cmd_port_t s_debug = { .port = &s_uart };
static cmd_port_t s_ble   = { .port = &s_bt };

static uint32_t s_key_press[KEY_COUNT];
static int      s_key_last = -1;
static uint32_t s_rx_total;
static uint32_t s_tx_total;

static void cmd_feed(cmd_port_t *c, const uint8_t *d, size_t n);

static void uart_write_str(const char *s)
{
    size_t n = strlen(s);

    /* Bounded, but not zero: with PM_UART_TIMEOUT_NOW the tail of a long line
     * is dropped as soon as the ring is full, which silently corrupted the
     * BLE traffic mirror. 50 ms at 115200 is ~570 bytes. */
    (void)pm_uart_write(&s_uart, (const uint8_t *)s, n, NULL, 50u);
    s_tx_total += (uint32_t)n;
}

static void uart_write_u32(uint32_t v)
{
    char  buf[11];
    char *p = &buf[sizeof(buf) - 1];

    *p = '\0';
    do {
        *--p = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    uart_write_str(p);
}

static void uart_write_hex(const uint8_t *d, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    char   out[4];
    size_t i;

    for (i = 0u; i < n; i++) {
        out[0] = ' ';
        out[1] = hex[(d[i] >> 4) & 0x0Fu];
        out[2] = hex[d[i] & 0x0Fu];
        out[3] = '\0';
        uart_write_str(out);
    }
}

static void on_uart_event(pm_uart_t *u, const pm_evt_t *evt, void *ctx)
{
    PM_UNUSED(u);
    PM_UNUSED(ctx);

    if (evt->id == PM_EVT_UART_ERROR) {
        s_uart_errors++;
    }
}

static void uart_service(void)
{
    uint8_t buf[32];
    size_t  got = 0u;

    while (pm_uart_read(&s_uart, buf, sizeof(buf), &got) == PM_OK) {
        if (got == 0u) {
            break;
        }
        cmd_feed(&s_debug, buf, got);
    }

    while (pm_uart_poll(&s_uart, NULL, on_uart_event, NULL)) {
        /* drain one event per call */
    }
}

static void key_service(uint32_t now)
{
    size_t i;

    for (i = 0u; i < KEY_COUNT; i++) {
        key_state_t *k = &s_keys[i];
        pm_gpio_level_t level = PM_GPIO_LEVEL_HIGH;
        uint8_t pressed;

        if (pm_gpio_read(k->pin, &level) != PM_OK) {
            continue;
        }
        pressed = (level == PM_GPIO_LEVEL_LOW) ? 1u : 0u;

        if (pressed != k->raw) {
            k->raw = pressed;
            k->raw_since = now;
        } else if ((pressed != k->stable) &&
                   ((uint32_t)(now - k->raw_since) >= KEY_DEBOUNCE_MS)) {
            k->stable = pressed;
            if (pressed != 0u) {
                s_key_press[i]++;
                s_key_last = (int)i;
            }
            (void)pm_gpio_write(k->led,
                                (pressed != 0u) ? PM_GPIO_LEVEL_HIGH
                                                : PM_GPIO_LEVEL_LOW);
            uart_write_str(k->name);
            uart_write_str((pressed != 0u) ? " pressed\r\n" : " released\r\n");
        }
    }
}

static void error_loop(void)
{
    pm_gpio_pin_t red = { .id = BOARD_GPIO_LED_RED };

    for (;;) {
        (void)pm_gpio_toggle(red);
        pm_osal_delay_ms(100u);
    }
}

/* ------------------------------------------------------- command channel
 * One parser shared by USART2 (debug) and USART3 (BLE). Lines end with CR
 * and/or LF, commands are case-insensitive, and every reply goes back to the
 * port the command arrived on. Replies are single-line key=value pairs with
 * CRLF, no decoration, so both a human and a program can parse them.
 * No dynamic allocation is used anywhere. */

/* pm_uart_write() drops the tail when the TX ring stays full until the
 * timeout, which silently truncated long replies (HELP is 274 B and the BLE
 * ring was 128 B). Keep pushing until every byte is accepted. */
static void tx_bytes_all(cmd_port_t *c, const uint8_t *d, size_t n)
{
    size_t off = 0u;

    while (off < n) {
        size_t   w  = 0u;
        pm_err_t rc = pm_uart_write(c->port, &d[off], n - off, &w, 1000u);

        off += w;
        if ((rc != PM_OK) && (w == 0u)) {
            break;
        }
    }
    s_tx_total += (uint32_t)off;

    /* Mirror what actually left the BLE port so a COM11 capture shows both
     * directions of the USART3 path, not just the RX side. */
    if (c == &s_ble) {
        uart_write_str("BT tx n=");
        uart_write_u32((uint32_t)off);
        uart_write_str("B:");
        uart_write_hex(d, off);
        uart_write_str("\r\n");
    }
}

static void tx_str(cmd_port_t *c, const char *s)
{
    tx_bytes_all(c, (const uint8_t *)s, strlen(s));
}

static void tx_u32(cmd_port_t *c, uint32_t v)
{
    char  buf[11];
    char *p = &buf[sizeof(buf) - 1];

    *p = '\0';
    do {
        *--p = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    tx_str(c, p);
}

static void tx_hex_byte(cmd_port_t *c, uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    char out[3];

    out[0] = hex[(b >> 4) & 0x0Fu];
    out[1] = hex[b & 0x0Fu];
    out[2] = '\0';
    tx_str(c, out);
}

static int ci_equal(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        char ca = *a++;
        char cb = *b++;

        if ((ca >= 'a') && (ca <= 'z')) {
            ca = (char)(ca - 'a' + 'A');
        }
        if ((cb >= 'a') && (cb <= 'z')) {
            cb = (char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return (*a == '\0') && (*b == '\0');
}

/* -------------------------------------------------------------- I2C1 scan
 * Bring-up only: polling access on purpose (the model has no I2C driver yet)
 * so the bus can be exercised without touching the build system. PB6 = SCL,
 * PB7 = SDA, 100 kHz from APB1 = 36 MHz. Reported addresses are 8-bit write
 * addresses (7-bit << 1), matching the OLED 0x7A / AHT20 0x70 convention. */

#define I2C_SPIN 200000u

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
        pm_osal_delay_ms(1u);
        GPIOB->BSRR = (1u << 6);
        pm_osal_delay_ms(1u);
    }
    GPIOB->BRR = (1u << 6);
    pm_osal_delay_ms(1u);
    GPIOB->BSRR = (1u << 6);
    pm_osal_delay_ms(1u);

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

static void cmd_i2c(cmd_port_t *c)
{
    uint8_t found[8];
    size_t  n;
    size_t  i;

    n = i2c1_scan(found, sizeof(found));
    tx_str(c, "I2C found=");
    if (n == 0u) {
        tx_str(c, "none\r\n");
        return;
    }
    for (i = 0u; i < n; i++) {
        if (i != 0u) {
            tx_str(c, ",");
        }
        tx_str(c, "0x");
        tx_hex_byte(c, found[i]);
    }
    tx_str(c, "\r\n");
}

/* ------------------------------------------------ AHT20 temperature/humidity
 * Standard flow from the AHT20 datasheet: trigger 0xAC/0x33/0x00, wait ~80 ms,
 * then read 6 bytes. The read is issued as a repeated START while the bus is
 * still owned by this master, which is the path that mock tests never touched.
 * Conversion (datasheet 6.2):
 *   RH% = raw_h / 2^20 * 100
 *   T C = raw_t / 2^20 * 200 - 50
 * Reported as tenths so no float support is needed. */

static int i2c1_read_after_trigger(uint8_t addr8r, uint8_t *rd, size_t rn)
{
    size_t i;

    I2C1->CR1 |= I2C_CR1_START;                     /* repeated START */
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

/* Trigger one conversion. repeated != 0 keeps bus ownership (repeated START
 * read, the path mock tests never covered); repeated == 0 ends the command
 * with STOP, which is what the AHT20 datasheet timing assumes. */
static int aht_trigger_only(int repeated)
{
    i2c1_bus_init();
    if (aht20_trigger() == 0) {
        i2c1_stop_and_wait();
        I2C1->CR1 &= ~I2C_CR1_PE;
        return 0;
    }
    if (repeated == 0) {
        i2c1_stop_and_wait();
    }
    return 1;
}

/* The 80 ms conversion window runs from the main loop, not inside the command
 * handler: a command must never block the heartbeat or the other port, and it
 * must always produce exactly one reply. */
#define AHT_WAIT_MS 80u
#define AHT_TRIES   3u

typedef struct {
    cmd_port_t *c;          /* where the reply goes */
    uint32_t    t0;
    uint8_t     active;
    uint8_t     repeated;
    uint8_t     attempt;
} aht_job_t;

static aht_job_t s_aht;

static void tx_fixed1(cmd_port_t *c, int32_t v)
{
    if (v < 0) {
        tx_str(c, "-");
        v = -v;
    }
    tx_u32(c, (uint32_t)(v / 10));
    tx_str(c, ".");
    tx_u32(c, (uint32_t)(v % 10));
}

static void aht_reply_raw(cmd_port_t *c, const uint8_t *d)
{
    size_t i;

    tx_str(c, " raw=");
    for (i = 0u; i < 6u; i++) {
        if (i != 0u) {
            tx_str(c, " ");
        }
        tx_hex_byte(c, d[i]);
    }
}

static void aht_reply_err(cmd_port_t *c, const uint8_t *d, const char *why)
{
    tx_str(c, "AHT err=");
    tx_str(c, why);
    aht_reply_raw(c, d);
    tx_str(c, "\r\n");
}

static void aht_reply_ok(cmd_port_t *c, const uint8_t *d, int repeated)
{
    uint32_t raw_h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | ((uint32_t)d[3] >> 4);
    uint32_t raw_t = (((uint32_t)d[3] & 0x0Fu) << 16) | ((uint32_t)d[4] << 8) | (uint32_t)d[5];
    uint32_t rh10  = (raw_h * 1000u) / 1048576u;
    int32_t  t10   = (int32_t)((raw_t * 2000u) / 1048576u) - 500;

    tx_str(c, "AHT ok rh=");
    tx_fixed1(c, (int32_t)rh10);
    tx_str(c, " t=");
    tx_fixed1(c, t10);
    aht_reply_raw(c, d);
    tx_str(c, " rst=");
    tx_u32(c, (uint32_t)repeated);
    tx_str(c, "\r\n");
}

static void aht_start(cmd_port_t *c, int repeated)
{
    static const uint8_t zero[6] = { 0u, 0u, 0u, 0u, 0u, 0u };

    if (s_aht.active != 0u) {
        tx_str(c, "AHT err=busy pending\r\n");
        return;
    }
    if (aht_trigger_only(repeated) == 0) {
        aht_reply_err(c, zero, "io");
        return;
    }
    s_aht.c        = c;
    s_aht.repeated = (uint8_t)repeated;
    s_aht.attempt  = 0u;
    s_aht.t0       = pm_osal_now_ms();
    s_aht.active   = 1u;
}

static void aht_service(uint32_t now)
{
    uint8_t    d[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    int        read_ok;
    const char *why;

    if (s_aht.active == 0u) {
        return;
    }
    if ((uint32_t)(now - s_aht.t0) < AHT_WAIT_MS) {
        return;
    }

    read_ok = i2c1_read_after_trigger(0x71u, d, 6u);
    if (read_ok == 0) {
        i2c1_stop_and_wait();
    }
    I2C1->CR1 &= ~I2C_CR1_PE;

    if ((read_ok != 0) && ((d[0] & 0x80u) == 0u)) {
        s_aht.active = 0u;
        aht_reply_ok(s_aht.c, d, (int)s_aht.repeated);
        return;
    }
    /* Keep the reason that matches the data actually observed. */
    why = ((read_ok != 0) && ((d[0] & 0x80u) != 0u)) ? "busy" : "io";

    s_aht.attempt++;
    if (s_aht.attempt >= AHT_TRIES) {
        s_aht.active = 0u;
        aht_reply_err(s_aht.c, d, why);
        return;
    }
    if (aht_trigger_only((int)s_aht.repeated) == 0) {
        s_aht.active = 0u;
        aht_reply_err(s_aht.c, d, why);
        return;
    }
    s_aht.t0 = now;
}

/* ---------------------------------------------- HC-SR04 distance, PA11/PA10
 * Board header (HARDWARE.md §3 第 6 条 + 附录引脚表): 1=GND, 2=PA10 (ECHO),
 * 3=PA11 (TRIG), 4=3.3V. PA10/PA11 are FT (5 V tolerant) on STM32F103x8/xB,
 * and this header feeds the module 3.3 V (HC-SR04P), so ECHO swings 0..3.3 V
 * here; no divider is fitted on the board.
 * ECHO is captured by EXTI10 and timestamped by TIM2 at 1 MHz, so DIST never
 * blocks: it fires the 10 us pulse and replies later from dist_service() when
 * the falling edge or the timeout arrives. */

#define DIST_TRIG_BIT    (1u << 11)
#define DIST_ECHO_BIT    (1u << 10)
#define DIST_TIMEOUT_MS  60u
#define DIST_MIN_US      100u      /* < 2 cm: noise */
#define DIST_MAX_US      25000u    /* > 4.3 m: the module tops out at 4 m */

typedef struct {
    cmd_port_t       *c;
    uint32_t          t0_ms;
    volatile uint16_t rise_us;
    volatile uint16_t width_us;
    volatile uint8_t  have_rise;
    volatile uint8_t  done;
    uint8_t           active;
    uint8_t           auto_live;   /* this measurement was started by live mode */
} dist_job_t;

static dist_job_t s_dist;

/* Last good distance, kept for OLED show. Cleared on a timeout so the screen
 * never claims a stale reading. */
static uint32_t s_dist_last_cm10;
static uint8_t  s_dist_last_ok;

/* Live mode measures on its own at 1 Hz, offset by half an OLED period from the
 * display refresh (see dist_live_tick()). */
#define DIST_LIVE_PERIOD_MS 1000u
static uint32_t s_dist_live_next;

static void dist_hw_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /* PA11 push-pull output 2 MHz, PA10 floating input */
    GPIOA->CRH = (GPIOA->CRH & ~((0xFu << 12) | (0xFu << 8)))
               | (0x2u << 12) | (0x4u << 8);
    GPIOA->BRR = DIST_TRIG_BIT;

    /* TIM2: 72 MHz / 72 = 1 MHz, free running, 65.5 ms wrap (handled) */
    TIM2->PSC = 71u;
    TIM2->ARR = 0xFFFFu;
    TIM2->CNT = 0u;
    TIM2->CR1 = TIM_CR1_CEN;

    /* EXTI10 <- port A, both edges */
    AFIO->EXTICR[2] = (AFIO->EXTICR[2] & ~(0xFu << 8));
    EXTI->IMR  |= DIST_ECHO_BIT;
    EXTI->RTSR |= DIST_ECHO_BIT;
    EXTI->FTSR |= DIST_ECHO_BIT;
    EXTI->PR    = DIST_ECHO_BIT;
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void EXTI15_10_IRQHandler(void)
{
    if ((EXTI->PR & DIST_ECHO_BIT) == 0u) {
        return;
    }
    EXTI->PR = DIST_ECHO_BIT;              /* write 1 to clear */

    if ((GPIOA->IDR & DIST_ECHO_BIT) != 0u) {
        s_dist.rise_us   = (uint16_t)TIM2->CNT;
        s_dist.have_rise = 1u;
    } else if (s_dist.have_rise != 0u) {
        s_dist.width_us  = (uint16_t)((uint16_t)TIM2->CNT - s_dist.rise_us);
        s_dist.have_rise = 0u;
        s_dist.done      = 1u;
    }
}

static void dist_start(cmd_port_t *c, uint8_t auto_live)
{
    uint16_t t0;

    if (s_dist.active != 0u) {
        if (auto_live != 0u) {
            return;                        /* live skips this period */
        }
        /* Manual DIST must not run a second HC-SR04 sequence next to the
         * measurement live started. */
        tx_str(c, (s_dist.auto_live != 0u) ? "DIST err=busy live\r\n"
                                           : "DIST err=busy\r\n");
        return;
    }

    s_dist.c         = c;
    s_dist.auto_live = auto_live;
    s_dist.done      = 0u;
    s_dist.have_rise = 0u;
    s_dist.width_us  = 0u;
    EXTI->PR = DIST_ECHO_BIT;

    /* 10 us TRIG pulse, timed by TIM2 so it does not depend on loop speed. */
    t0 = (uint16_t)TIM2->CNT;
    GPIOA->BSRR = DIST_TRIG_BIT;
    while ((uint16_t)((uint16_t)TIM2->CNT - t0) < 10u) {
    }
    GPIOA->BRR = DIST_TRIG_BIT;

    s_dist.t0_ms  = pm_osal_now_ms();
    s_dist.active = 1u;
}

static void dist_service(uint32_t now)
{
    uint32_t us;
    uint32_t cm10;

    if (s_dist.active == 0u) {
        return;
    }

    if (s_dist.done == 0u) {
        if ((uint32_t)(now - s_dist.t0_ms) < DIST_TIMEOUT_MS) {
            return;
        }
        s_dist.active    = 0u;
        s_dist.have_rise = 0u;
        s_dist_last_ok   = 0u;
        if (s_dist.auto_live != 0u) {
            tx_str(s_dist.c, "DIST live ");
            s_dist.auto_live = 0u;
        } else {
            tx_str(s_dist.c, "DIST ");
        }
        tx_str(s_dist.c, "err=timeout\r\n");
        return;
    }

    us = (uint32_t)s_dist.width_us;
    s_dist.active = 0u;

    if ((us < DIST_MIN_US) || (us > DIST_MAX_US)) {
        /* HC-SR04 holds ECHO high ~38 ms when nothing reflects the burst. */
        s_dist_last_ok = 0u;
        if (s_dist.auto_live != 0u) {
            tx_str(s_dist.c, "DIST live ");
            s_dist.auto_live = 0u;
        } else {
            tx_str(s_dist.c, "DIST ");
        }
        tx_str(s_dist.c, "err=timeout\r\n");
        return;
    }

    cm10 = (us * 10u + 29u) / 58u;         /* us / 58 => cm, kept x10 */

    s_dist_last_cm10 = cm10;
    s_dist_last_ok   = 1u;

    if (s_dist.auto_live != 0u) {
        tx_str(s_dist.c, "DIST live ");
        s_dist.auto_live = 0u;
    } else {
        tx_str(s_dist.c, "DIST ");
    }
    tx_str(s_dist.c, "cm=");
    tx_fixed1(s_dist.c, (int32_t)cm10);
    tx_str(s_dist.c, " us=");
    tx_u32(s_dist.c, us);
    tx_str(s_dist.c, "\r\n");
}

/* ----------------------------------------------------- OLED (CH1116, 0x7A)
 * HARDWARE.md item 9 / section 7 item 8: CH1116 at 0x7A (8-bit write address,
 * i.e. 7-bit 0x3D), SCL = PB6, SDA = PB7, shared with the AHT20. The module
 * spec is ZJY130-2864KSWLG01 (1.3 inch, "2864" = 128x64 dots).
 *
 * Bus access is bounded polling of the I2C1 status register, not an interrupt
 * state machine: the peripheral has no I2C IRQ wired up and adding one would
 * change the port layer for a single device. One service call pushes a single
 * 128-byte page = 130 bytes on the wire, ~12 ms at 100 kHz. A full screen is
 * split over 8 calls (8 main loop iterations), so the loop is never blocked
 * for a whole-frame transfer.
 */

#define OLED_FRAME_SPIN 50000u
#define OLED_LIVE_PERIOD_MS 500u

static oled_ch1116_t s_oled;
static uint32_t      s_oled_af;
static uint32_t      s_oled_berr;
static uint32_t      s_oled_to;

static cmd_port_t *s_oled_req;         /* NULL = no job running */
static const char *s_oled_name;
static uint32_t    s_oled_t0;

/* `OLED live on`: refresh the status lines every OLED_LIVE_PERIOD_MS. One job
 * at a time; a period is skipped rather than queued when the panel is busy. */
static uint8_t     s_oled_live;        /* 1 = periodic refresh enabled */
static uint8_t     s_oled_live_job;    /* the running job is a live refresh */
static cmd_port_t *s_oled_live_port;   /* where live refresh replies go */
static uint32_t    s_oled_live_next;   /* ms of the next refresh */

static int oled_i2c_wait(uint32_t mask)
{
    uint32_t t = OLED_FRAME_SPIN;

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
        I2C1->DR = OLED_CH1116_ADDR8;
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

static void oled_tx_counters(cmd_port_t *c)
{
    tx_str(c, " af=");
    tx_u32(c, s_oled_af);
    tx_str(c, " berr=");
    tx_u32(c, s_oled_berr);
    tx_str(c, " to=");
    tx_u32(c, s_oled_to);
}

static void oled_err_reply(cmd_port_t *c, const char *why)
{
    tx_str(c, "OLED err=");
    tx_str(c, why);
    oled_tx_counters(c);
    tx_str(c, "\r\n");
}

/* 1 when line starts with pfx (case-insensitive); *rest points just past it. */
static int ci_starts(const char *line, const char *pfx, const char **rest)
{
    const char *p = line;

    while (*pfx != '\0') {
        char a = *p;
        char b = *pfx;

        if ((a >= 'a') && (a <= 'z')) {
            a = (char)(a - 'a' + 'A');
        }
        if ((b >= 'a') && (b <= 'z')) {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
        p++;
        pfx++;
    }
    *rest = p;
    return 1;
}

/* Strict decimal: optional spaces, digits, optional spaces, end. */
static int parse_u32(const char *s, uint32_t *out)
{
    uint32_t v   = 0u;
    int      any = 0;

    while (*s == ' ') {
        s++;
    }
    while ((*s >= '0') && (*s <= '9')) {
        v = (v * 10u) + (uint32_t)(*s - '0');
        any = 1;
        s++;
    }
    if (any == 0) {
        return 0;
    }
    while (*s == ' ') {
        s++;
    }
    if (*s != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

static char *sput_u32(char *p, uint32_t v)
{
    char   tmp[11];
    size_t n = 0u;

    do {
        tmp[n] = (char)('0' + (v % 10u));
        v /= 10u;
        n++;
    } while (v != 0u);
    while (n != 0u) {
        n--;
        *p++ = tmp[n];
    }
    return p;
}

/* Stage the three STAT-ish lines. Pages 0..2, so they land on the top 24 rows. */
static void oled_stage_show(void)
{
    char    b[OLED_CH1116_LINE_CHARS + 1u];
    char   *p;

    p = b;
    *p++ = 'U'; *p++ = 'P'; *p++ = '=';
    p = sput_u32(p, pm_osal_now_ms() / 1000u);
    *p++ = 'S';
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 0u, b);

    p = b;
    *p++ = 'K'; *p++ = '1'; *p++ = '=';
    p = sput_u32(p, s_key_press[0]);
    *p++ = ' ';
    *p++ = 'K'; *p++ = '2'; *p++ = '=';
    p = sput_u32(p, s_key_press[1]);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 1u, b);

    if (s_dist_last_ok != 0u) {
        p = b;
        *p++ = 'D'; *p++ = 'I'; *p++ = 'S'; *p++ = 'T'; *p++ = '=';
        p = sput_u32(p, s_dist_last_cm10 / 10u);
        *p++ = '.';
        *p++ = (char)('0' + (s_dist_last_cm10 % 10u));
        *p++ = 'C'; *p++ = 'M';
        *p = '\0';
        oled_ch1116_stage_line(&s_oled, 2u, b);
    } else {
        oled_ch1116_stage_line(&s_oled, 2u, "DIST=NONE");
    }
}

/* Four live rows: uptime, key counters, last distance, I2C error counters. */
static void oled_stage_live(void)
{
    char    b[OLED_CH1116_LINE_CHARS + 1u];
    char   *p;

    p = b;
    *p++ = 'U'; *p++ = 'P'; *p++ = '=';
    p = sput_u32(p, pm_osal_now_ms() / 1000u);
    *p++ = 'S';
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 0u, b);

    p = b;
    *p++ = 'K'; *p++ = '1'; *p++ = '=';
    p = sput_u32(p, s_key_press[0]);
    *p++ = ' ';
    *p++ = 'K'; *p++ = '2'; *p++ = '=';
    p = sput_u32(p, s_key_press[1]);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 1u, b);

    if (s_dist_last_ok != 0u) {
        p = b;
        *p++ = 'D'; *p++ = 'I'; *p++ = 'S'; *p++ = 'T'; *p++ = '=';
        p = sput_u32(p, s_dist_last_cm10 / 10u);
        *p++ = '.';
        *p++ = (char)('0' + (s_dist_last_cm10 % 10u));
        *p++ = 'C'; *p++ = 'M';
        *p = '\0';
        oled_ch1116_stage_line(&s_oled, 2u, b);
    } else {
        oled_ch1116_stage_line(&s_oled, 2u, "DIST=NONE");
    }

    p = b;
    *p++ = 'I'; *p++ = '2'; *p++ = 'C'; *p++ = ' ';
    *p++ = 'a'; *p++ = 'f'; *p++ = '=';
    p = sput_u32(p, s_oled_af);
    *p++ = ' ';
    *p++ = 'b'; *p++ = 'e'; *p++ = 'r'; *p++ = 'r'; *p++ = '=';
    p = sput_u32(p, s_oled_berr);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 3u, b);
}

/* Every screen command paints all 8 pages, so the result never depends on what
 * was on the panel before. `OLED show` used to write only pages 0..2, which
 * left the lower half of a previous `frame` on screen and is exactly what the
 * photo showed. */
typedef enum {
    OLED_SCREEN_NONE = 0,
    OLED_SCREEN_CLEAR,
    OLED_SCREEN_TEST,
    OLED_SCREEN_FRAME,
    OLED_SCREEN_SHOW,
    OLED_SCREEN_LIVE
} oled_screen_t;

static oled_screen_t s_oled_last;

static const char *oled_screen_name(oled_screen_t s)
{
    switch (s) {
    case OLED_SCREEN_CLEAR: return "clear";
    case OLED_SCREEN_TEST:  return "test";
    case OLED_SCREEN_FRAME: return "frame";
    case OLED_SCREEN_SHOW:  return "show";
    case OLED_SCREEN_LIVE:  return "live";
    default:                return "none";
    }
}

/* Starts a full-screen job; pages 3..7 of a show are written blank. */
static int oled_start_screen(oled_screen_t s)
{
    switch (s) {
    case OLED_SCREEN_CLEAR:
        return oled_ch1116_start_pattern(&s_oled, 0x00u);
    case OLED_SCREEN_TEST:
        return oled_ch1116_start_pattern(&s_oled, 0xAAu);
    case OLED_SCREEN_FRAME:
        return oled_ch1116_start_frame(&s_oled);
    case OLED_SCREEN_SHOW:
        oled_stage_show();
        return oled_ch1116_start_text(&s_oled, 0u, (uint8_t)OLED_CH1116_PAGES);
    default:
        return -1;
    }
}

/* One live refresh: the same paged job as `show`, always all 8 pages so a row
 * that becomes empty (DIST value -> NONE) leaves no residue. The reply is sent
 * when the job completes, so ms= is the real 8-page transfer time. */
static int oled_start_live_job(cmd_port_t *port, const char *name, uint32_t now)
{
    oled_stage_live();
    if (oled_ch1116_start_text(&s_oled, 0u, (uint8_t)OLED_CH1116_PAGES) != 0) {
        return -1;
    }
    s_oled_req      = port;
    s_oled_name     = name;
    s_oled_t0       = now;
    s_oled_live_job = 1u;
    return 0;
}

static void cmd_oled(cmd_port_t *c, const char *arg)
{
    oled_screen_t s = OLED_SCREEN_NONE;
    const char   *rest = NULL;

    /* `live` owns the panel; `show` would fight the periodic refresh. */
    if ((s_oled_live != 0u) && ci_equal(arg, "SHOW")) {
        tx_str(c, "OLED err=busy live\r\n");
        return;
    }

    /* A pattern command or `live off` takes the panel back even in the middle
     * of a live refresh. Command handling and oled_service() both run in the
     * main loop, so this always lands on a page boundary and no I2C frame is
     * cut in half. */
    if (s_oled_live_job != 0u) {
        int takeover = ci_equal(arg, "FRAME") || ci_equal(arg, "TEST") ||
                       ci_equal(arg, "CLEAR") ||
                       ci_starts(arg, "LIVE ", &rest) ||
                       ci_starts(arg, "COL", &rest);

        if (takeover != 0) {
            oled_ch1116_abort(&s_oled);
            s_oled_req      = NULL;
            s_oled_live_job = 0u;
        }
    }

    if (s_oled_req != NULL) {
        tx_str(c, "OLED err=busy pending\r\n");
        return;
    }
    /* AHTS keeps bus ownership across its 80 ms window; do not cut in. */
    if (s_aht.active != 0u) {
        tx_str(c, "OLED err=busy i2c\r\n");
        return;
    }

    if (ci_equal(arg, "ON")) {
        if (oled_ch1116_display(&s_oled, 1) != 0) {
            oled_err_reply(c, "io");
            return;
        }
        tx_str(c, "OLED on ok");
        oled_tx_counters(c);
        tx_str(c, "\r\n");
        return;
    }
    if (ci_equal(arg, "OFF")) {
        if (oled_ch1116_display(&s_oled, 0) != 0) {
            oled_err_reply(c, "io");
            return;
        }
        tx_str(c, "OLED off ok");
        oled_tx_counters(c);
        tx_str(c, "\r\n");
        return;
    }

    /* `OLED live on|off`; a bare `OLED live` is a usage error. */
    if (ci_equal(arg, "LIVE")) {
        tx_str(c, "OLED err=usage\r\n");
        return;
    }
    if (ci_starts(arg, "LIVE ", &rest)) {
        while (*rest == ' ') {
            rest++;
        }
        if (ci_equal(rest, "ON")) {
            s_oled_live      = 1u;
            s_oled_live_port = c;
            s_oled_last      = OLED_SCREEN_LIVE;
            s_oled_live_next = pm_osal_now_ms();
            /* Half a display period later: the 10 us TRIG pulse and the <=23 ms
             * echo window then fall between page transfers, not inside one. */
            s_dist_live_next = s_oled_live_next + (OLED_LIVE_PERIOD_MS / 2u);
            tx_str(c, "OLED live=on period=");
            tx_u32(c, OLED_LIVE_PERIOD_MS);
            tx_str(c, "ms\r\n");
            return;
        }
        if (ci_equal(rest, "OFF")) {
            s_oled_live = 0u;
            tx_str(c, "OLED live=off\r\n");
            return;
        }
        tx_str(c, "OLED err=usage\r\n");
        return;
    }

    /* Diagnostic for the 132-column RAM / 128-column glass offset. Repaints the
     * last screen so the A/B needs no extra `frame` command. */
    if (ci_starts(arg, "COL", &rest)) {
        uint32_t v;

        if ((parse_u32(rest, &v) == 0) || (v > 4u)) {
            tx_str(c, "OLED err=usage col=0..4\r\n");
            return;
        }
        oled_ch1116_set_col_offset(&s_oled, (uint8_t)v);
        tx_str(c, "OLED col=");
        tx_u32(c, v);
        tx_str(c, " ok");
        if (s_oled_last == OLED_SCREEN_LIVE) {
            uint32_t now = pm_osal_now_ms();

            if (oled_start_live_job(c, "redraw", now) == 0) {
                s_oled_live_next = now + OLED_LIVE_PERIOD_MS;
                tx_str(c, " redraw=live");
            }
        } else if (s_oled_last != OLED_SCREEN_NONE) {
            if (oled_start_screen(s_oled_last) == 0) {
                s_oled_req  = c;
                s_oled_name = "redraw";
                s_oled_t0   = pm_osal_now_ms();
                tx_str(c, " redraw=");
                tx_str(c, oled_screen_name(s_oled_last));
            }
        }
        tx_str(c, "\r\n");
        return;
    }

    if (ci_equal(arg, "CLEAR")) {
        s = OLED_SCREEN_CLEAR;
    } else if (ci_equal(arg, "TEST")) {
        s = OLED_SCREEN_TEST;
    } else if (ci_equal(arg, "FRAME")) {
        s = OLED_SCREEN_FRAME;
    } else if (ci_equal(arg, "SHOW")) {
        s = OLED_SCREEN_SHOW;
    } else {
        tx_str(c, "OLED err=usage\r\n");
        return;
    }

    /* frame / test / clear take the panel back from live. */
    s_oled_live = 0u;

    if (oled_start_screen(s) != 0) {
        tx_str(c, "OLED err=busy pending\r\n");
        return;
    }

    s_oled_last  = s;
    s_oled_req   = c;
    s_oled_name  = oled_screen_name(s);
    s_oled_t0    = pm_osal_now_ms();
}

/* Called once per main loop iteration: at most one page per call. */
static void oled_service(void)
{
    cmd_port_t *c;
    int         was_live;

    if (s_oled_req == NULL) {
        return;
    }
    if (oled_ch1116_service(&s_oled) != 0) {
        return;
    }

    c           = s_oled_req;
    was_live    = (int)s_oled_live_job;
    s_oled_req  = NULL;
    s_oled_live_job = 0u;

    if (oled_ch1116_err(&s_oled) != 0) {
        oled_err_reply(c, "io");
        return;
    }
    tx_str(c, "OLED ");
    tx_str(c, s_oled_name);
    tx_str(c, " ok ms=");
    tx_u32(c, (uint32_t)(pm_osal_now_ms() - s_oled_t0));
    tx_str(c, " col=");
    tx_u32(c, (uint32_t)oled_ch1116_col_offset(&s_oled));
    /* A periodic refresh already implies live=on; pattern commands report that
     * they took the panel back. */
    if (was_live == 0) {
        tx_str(c, " live=");
        tx_str(c, (s_oled_live != 0u) ? "on" : "off");
    }
    oled_tx_counters(c);
    tx_str(c, "\r\n");
}

/* Called once per main loop iteration. One refresh per 500 ms; when the panel
 * is still busy the period is skipped, never queued. */
static void oled_live_tick(uint32_t now)
{
    if (s_oled_live == 0u) {
        return;
    }
    if (s_oled_req != NULL) {
        return;
    }
    if ((int32_t)(now - s_oled_live_next) < 0) {
        return;
    }
    s_oled_live_next = now + OLED_LIVE_PERIOD_MS;
    (void)oled_start_live_job(s_oled_live_port, "live", now);
}

/* Live mode triggers the HC-SR04 itself: one non-blocking dist_start() per
 * second, reusing the manual path (no second copy of the sequence). It is
 * offset half an OLED period from the display refresh, so the 10 us pulse and
 * the <=23 ms echo window sit in the gap between two page transfers. The result
 * goes into s_dist_last_* and the next refresh (<=500 ms later) stages it; a
 * measurement that is still in flight just skips this period. */
static void dist_live_tick(uint32_t now)
{
    if (s_oled_live == 0u) {
        return;
    }
    if ((int32_t)(now - s_dist_live_next) < 0) {
        return;
    }
    s_dist_live_next = now + DIST_LIVE_PERIOD_MS;
    dist_start(s_oled_live_port, 1u);
}

/* Returns the argument after "OLED" ("" for a bare OLED), or NULL if the line
 * is a different command. */
static const char *oled_arg(const char *line)
{
    const char *p = line;
    size_t      i;

    for (i = 0u; i < 4u; i++) {
        char a = p[i];

        if ((a >= 'a') && (a <= 'z')) {
            a = (char)(a - 'a' + 'A');
        }
        if (a != "OLED"[i]) {
            return NULL;
        }
    }
    p = &line[4];
    if (*p == '\0') {
        return p;
    }
    if (*p != ' ') {
        return NULL;
    }
    while (*p == ' ') {
        p++;
    }
    return p;
}

static void cmd_execute(cmd_port_t *c, const char *line)
{
    if (ci_equal(line, "PING")) {
        tx_str(c, "PONG\r\n");
    } else if (ci_equal(line, "HELP")) {
        tx_str(c, "HELP HELP=list commands\r\n");
        tx_str(c, "HELP PING=reply PONG\r\n");
        tx_str(c, "HELP STAT=clk/up/tick/keys/uart counters\r\n");
        tx_str(c, "HELP I2C=scan I2C1 bus, list 8-bit addresses\r\n");
        tx_str(c, "HELP AHT=read AHT20, rh and t in 0.1 units\r\n");
        tx_str(c, "HELP AHTS=same but read via repeated START\r\n");
        tx_str(c, "HELP DIST=one shot, cm and us; live=1Hz\r\n");
        tx_str(c, "HELP OLED=on/off/clear/test/show/frame/col/live\r\n");
        tx_str(c, "HELP KEYS=key press counters, clears them\r\n");
    } else if (ci_equal(line, "STAT")) {
        tx_str(c, "STAT clk=");
        tx_u32(c, pm_platform_core_clock());
        tx_str(c, " up=");
        tx_u32(c, pm_osal_now_ms());
        tx_str(c, " tick=1 key1=");
        tx_u32(c, s_key_press[0]);
        tx_str(c, " key2=");
        tx_u32(c, s_key_press[1]);
        tx_str(c, " uart_rx=");
        tx_u32(c, s_rx_total);
        tx_str(c, " uart_tx=");
        tx_u32(c, s_tx_total);
        tx_str(c, "\r\n");
    } else if (ci_equal(line, "I2C")) {
        cmd_i2c(c);
    } else if (ci_equal(line, "AHT")) {
        aht_start(c, 0);
    } else if (ci_equal(line, "AHTS")) {
        aht_start(c, 1);
    } else if (ci_equal(line, "DIST")) {
        dist_start(c, 0u);
    } else if (ci_equal(line, "KEYS")) {
        tx_str(c, "KEYS k1=");
        tx_u32(c, s_key_press[0]);
        tx_str(c, " k2=");
        tx_u32(c, s_key_press[1]);
        tx_str(c, " last=");
        tx_str(c, (s_key_last == 0) ? "k1" : (s_key_last == 1) ? "k2" : "none");
        tx_str(c, "\r\n");
        s_key_press[0] = 0u;
        s_key_press[1] = 0u;
        s_key_last = -1;
    } else {
        const char *oa = oled_arg(line);

        if (oa != NULL) {
            cmd_oled(c, oa);
            return;
        }
        tx_str(c, "ERR unknown=");
        tx_str(c, line);
        tx_str(c, "\r\n");
    }
}

static void cmd_feed(cmd_port_t *c, const uint8_t *d, size_t n)
{
    size_t i;

    s_rx_total += (uint32_t)n;
    c->last_ms = pm_osal_now_ms();
    for (i = 0u; i < n; i++) {
        uint8_t b = d[i];

        if ((b == (uint8_t)'\r') || (b == (uint8_t)'\n')) {
            if (c->len != 0u) {
                c->line[c->len] = '\0';
                cmd_execute(c, c->line);
                c->len = 0u;
            }
            continue;
        }
        if ((b >= 0x20u) && (b < 0x7Fu)) {
            if (c->len < (CMD_LINE_MAX - 1u)) {
                c->line[c->len] = (char)b;
                c->len++;
            }
        }
    }
}

/* A browser/Web Bluetooth client may send "AHT" without any CR/LF (the page's
 * line ending is configurable, and DX-BT24 forwards BLE packets verbatim).
 * Requiring a terminator left the token sitting in the buffer and produced no
 * reply at all. Any line that stops growing for CMD_IDLE_MS is executed. */
#define CMD_IDLE_MS 100u

static void cmd_flush_idle(cmd_port_t *c, uint32_t now)
{
    if ((c->len == 0u) || ((uint32_t)(now - c->last_ms) < CMD_IDLE_MS)) {
        return;
    }
    c->line[c->len] = '\0';
    c->len = 0u;
    cmd_execute(c, c->line);
}

/* One-shot bring-up: init sequence, then a synchronous clear so the panel
 * starts from a known state. 8 pages, ~100 ms, once at boot. */
static void oled_boot(void)
{
    uint8_t guard = 0u;

    if (oled_ch1116_init(&s_oled, oled_bus_write, NULL) != 0) {
        uart_write_str("OLED CH1116 addr=7A init FAILED\r\n");
        return;
    }
    uart_write_str("OLED CH1116 addr=7A init ok, col=");
    uart_write_u32((uint32_t)oled_ch1116_col_offset(&s_oled));
    uart_write_str(", clearing\r\n");
    (void)oled_ch1116_start_pattern(&s_oled, 0x00u);
    while ((oled_ch1116_service(&s_oled) != 0) &&
           (guard < (uint8_t)OLED_CH1116_PAGES)) {
        guard++;
    }
    (void)oled_ch1116_display(&s_oled, 1);
    if (oled_ch1116_err(&s_oled) != 0) {
        uart_write_str("OLED clear FAILED\r\n");
        return;
    }
    /* The boot clear is a real screen state: `OLED col n` repaints it so an
     * A/B before the first pattern command still shows a redraw. */
    s_oled_last = OLED_SCREEN_CLEAR;
    uart_write_str("OLED ready (128x64, 6x8 font)\r\n");
}

static void bt_init(void)
{
    if (pm_uart_init(&s_bt, &s_bt_cfg) == PM_OK) {
        uart_write_str("BT USART3 9600 8N1, command channel ready\r\n");
    } else {
        uart_write_str("BT USART3 init FAILED\r\n");
    }
}

static void bt_service(void)
{
    uint8_t buf[32];
    size_t  n = 0u;

    while (pm_uart_read(&s_bt, buf, sizeof(buf), &n) == PM_OK) {
        if (n == 0u) {
            break;
        }
        /* Mirror what the BLE side sent so a COM11 capture shows both paths. */
        uart_write_str("BT rx n=");
        uart_write_u32((uint32_t)n);
        uart_write_str("B:");
        uart_write_hex(buf, n);
        uart_write_str("\r\n");
        cmd_feed(&s_ble, buf, n);
    }
}

int main(void)
{
    pm_gpio_pin_t led_blue = { .id = BOARD_GPIO_LED_BLUE };
    size_t i;
    uint32_t next_led_ms;
    uint32_t next_hb_ms;

    pm_platform_init();

    if (pm_gpio_configure(led_blue, &s_led_cfg) != PM_OK) {
        error_loop();
    }
    for (i = 0u; i < KEY_COUNT; i++) {
        if (pm_gpio_configure(s_keys[i].pin, &s_key_cfg) != PM_OK) {
            error_loop();
        }
        if (pm_gpio_configure(s_keys[i].led, &s_led_cfg) != PM_OK) {
            error_loop();
        }
    }
    if (pm_uart_init(&s_uart, &s_uart_cfg) != PM_OK) {
        error_loop();
    }

    uart_write_str("\r\nboard-bringup\r\ncore clock = ");
    uart_write_u32(pm_platform_core_clock());
    uart_write_str(" Hz\r\nUSART2 115200 8N1, command channel ready\r\n");

    bt_init();
    dist_hw_init();
    oled_boot();

    next_led_ms = pm_osal_now_ms();
    next_hb_ms  = next_led_ms;

    for (;;) {
        uint32_t now = pm_osal_now_ms();
        uint32_t t;

        uart_service();
        key_service(now);
        bt_service();

        /* Fresh timestamp: cmd_feed() stamps last_ms and aht_start() stamps t0
         * *after* `now` was sampled. Comparing against the stale `now` makes
         * (now - last_ms) wrap to a huge value, which fired the idle flush and
         * the AHT window immediately. */
        t = pm_osal_now_ms();
        cmd_flush_idle(&s_debug, t);
        cmd_flush_idle(&s_ble, t);
        aht_service(t);
        dist_service(t);
        dist_live_tick(t);
        oled_service();
        oled_live_tick(pm_osal_now_ms());

        if ((uint32_t)(now - next_led_ms) >= 500u) {
            next_led_ms += 500u;
            (void)pm_gpio_toggle(led_blue);
        }
        if ((uint32_t)(now - next_hb_ms) >= 1000u) {
            next_hb_ms += 1000u;
            uart_write_str("hb\r\n");
        }
    }
}
