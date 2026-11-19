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
#include "pm_cmd.h"
#include "pm_cmd_out.h"
#include "pm_cmd_table.h"
#include "pm_i2c1.h"
#include "pm_platform.h"
#include "board_f103c8.h"
#include "oled_ch1116.h"
#include "aht20_dev.h"
#include "pwm_dev.h"
#include "track_dev.h"
#include "encoder_dev.h"
#include "relay_dev.h"
#include "motor_dev.h"

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
static cmd_port_t s_debug = { .port = &s_uart };
static cmd_port_t s_ble   = { .port = &s_bt };

static uint32_t s_key_press[KEY_COUNT];
static int      s_key_last = -1;
static uint32_t s_rx_total;

/* Declared here because aht_bg_tick() runs before the OLED block: the
 * background AHT trigger must not cut into a multi-page I2C push. */
static cmd_port_t *s_oled_req;         /* NULL = no job running */
static void oled_now_row_flash(void);  /* repaint the NOW row for a short job */

static void cmd_feed(cmd_port_t *c, const uint8_t *d, size_t n);

/* The AHT20 job lives in src/components/aht20_dev; these two hooks are the
 * only things it needs from the entry layer. */
static int aht_ui_busy(void)
{
    return (s_oled_req != NULL) ? 1 : 0;
}

static const aht20_hooks_t s_aht_hooks = {
    .ui_busy    = aht_ui_busy,
    .ui_now_row = oled_now_row_flash,
};

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
            pm_cmd_out_uart_write_str(k->name);
            pm_cmd_out_uart_write_str((pressed != 0u) ? " pressed\r\n" : " released\r\n");
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

/* -------------------------------------------------------------- I2C1 scan
 * The polling master (bus fix/init/scan) lives in
 * src/port/stm32f1/pm_i2c1_stm32f1.c; this file only formats the reply.
 * Reported addresses are 8-bit write addresses (7-bit << 1), matching the
 * OLED 0x7A / AHT20 0x70 convention. */

static void cmd_i2c(cmd_port_t *c)
{
    uint8_t found[8];
    size_t  n;
    size_t  i;

    n = pm_i2c1_ops()->scan(found, sizeof(found));
    pm_cmd_out_tx_str(c, "I2C found=");
    if (n == 0u) {
        pm_cmd_out_tx_str(c, "none\r\n");
        return;
    }
    for (i = 0u; i < n; i++) {
        if (i != 0u) {
            pm_cmd_out_tx_str(c, ",");
        }
        pm_cmd_out_tx_str(c, "0x");
        pm_cmd_out_tx_hex_byte(c, found[i]);
    }
    pm_cmd_out_tx_str(c, "\r\n");
}

/* ------------------------------------------------------------ ADC1, PA4/PA5
 * HARDWARE.md §3: PA4 = NTC (ADC1_IN4, beta 3950, 10 k divider),
 * PA5 = potentiometer (ADC1_IN5, 3.3 V/GND divider). Polling single-shot on
 * purpose, same bring-up style as the I2C1 scan above; there is no async ADC
 * model in this repository yet. ADCCLK = PCLK2 / 6 = 12 MHz (limit 14 MHz). */

static ADC_HandleTypeDef s_adc1;
static uint8_t           s_adc_ok;

static void adc_boot(void)
{
    GPIO_InitTypeDef g = { 0 };

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);
    g.Pin  = GPIO_PIN_4 | GPIO_PIN_5;
    g.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &g);

    s_adc1.Instance = ADC1;
    s_adc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    s_adc1.Init.ContinuousConvMode    = DISABLE;
    s_adc1.Init.DiscontinuousConvMode = DISABLE;
    s_adc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    s_adc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    s_adc1.Init.NbrOfConversion       = 1;
    if (HAL_ADC_Init(&s_adc1) != HAL_OK) {
        pm_cmd_out_uart_write_str("ADC1 init FAILED\r\n");
        return;
    }
    if (HAL_ADCEx_Calibration_Start(&s_adc1) != HAL_OK) {
        pm_cmd_out_uart_write_str("ADC1 calibration FAILED\r\n");
        return;
    }
    s_adc_ok = 1u;
    pm_cmd_out_uart_write_str("ADC1 PA4=IN4 ntc, PA5=IN5 pot, 12-bit\r\n");
}

/* One software-triggered conversion; 0 on any HAL failure. */
static int adc_sample(uint32_t channel, uint16_t *raw)
{
    ADC_ChannelConfTypeDef ch = { 0 };

    ch.Channel      = channel;
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    if (HAL_ADC_ConfigChannel(&s_adc1, &ch) != HAL_OK) {
        return 0;
    }
    if (HAL_ADC_Start(&s_adc1) != HAL_OK) {
        return 0;
    }
    if (HAL_ADC_PollForConversion(&s_adc1, 5u) != HAL_OK) {
        (void)HAL_ADC_Stop(&s_adc1);
        return 0;
    }
    *raw = (uint16_t)HAL_ADC_GetValue(&s_adc1);
    (void)HAL_ADC_Stop(&s_adc1);
    return 1;
}

/* Same rounding as pm_adc_to_mv() in the model repo: raw * 3300 / 4095. */
static uint32_t adc_to_mv(uint16_t raw)
{
    return ((uint32_t)raw * 3300u + 2047u) / 4095u;
}

/* Per-frame read for the live page. Both channels are behind a 55.5-cycle
 * sample, so this costs well under 1 ms; a failed read blanks the row instead
 * of showing a stale or zero value. */
static uint16_t s_live_pot;
static uint16_t s_live_ntc;
static uint8_t  s_live_adc_ok;

static void adc_cache_read(void)
{
    uint16_t pot;
    uint16_t ntc;

    s_live_adc_ok = 0u;
    if (adc_sample(ADC_CHANNEL_5, &pot) == 0) {
        return;
    }
    if (adc_sample(ADC_CHANNEL_4, &ntc) == 0) {
        return;
    }
    s_live_pot    = pot;
    s_live_ntc    = ntc;
    s_live_adc_ok = 1u;
}

static void cmd_adc(cmd_port_t *c)
{
    uint16_t pot = 0u;
    uint16_t ntc = 0u;

    if (s_adc_ok == 0u) {
        pm_cmd_out_last_set("ADC err=init");
        pm_cmd_out_tx_str(c, "ADC err=init\r\n");
        return;
    }
    if (adc_sample(ADC_CHANNEL_5, &pot) == 0) {
        pm_cmd_out_last_set("ADC err=tout pa5");
        pm_cmd_out_tx_str(c, "ADC err=timeout ch=pa5\r\n");
        return;
    }
    if (adc_sample(ADC_CHANNEL_4, &ntc) == 0) {
        pm_cmd_out_last_set("ADC err=tout pa4");
        pm_cmd_out_tx_str(c, "ADC err=timeout ch=pa4\r\n");
        return;
    }
    pm_cmd_out_last_num("ADC ok ", adc_to_mv(pot), "mV");
    pm_cmd_out_tx_str(c, "ADC pot=");
    pm_cmd_out_tx_u32(c, pot);
    pm_cmd_out_tx_str(c, " pot_mv=");
    pm_cmd_out_tx_u32(c, adc_to_mv(pot));
    pm_cmd_out_tx_str(c, " ntc=");
    pm_cmd_out_tx_u32(c, ntc);
    pm_cmd_out_tx_str(c, " ntc_mv=");
    pm_cmd_out_tx_u32(c, adc_to_mv(ntc));
    pm_cmd_out_tx_str(c, " vref=3300\r\n");
}

/* ---------------------------------------------- HC-SR04 distance, PA11/PA10
 * Board header (HARDWARE.md §3 第 6 条 + 附录引脚表): 1=GND, 2=PA10 (ECHO),
 * 3=PA11 (TRIG), 4=3.3V. PA10/PA11 are FT (5 V tolerant) on STM32F103x8/xB,
 * and this header feeds the module 3.3 V (HC-SR04P), so ECHO swings 0..3.3 V
 * here; no divider is fitted on the board.
 * ECHO is captured by EXTI10 and timestamped by TIM2 at 1 MHz, so DIST never
 * blocks: it fires the 10 us pulse and replies later from dist_service() when
 * the falling edge or the timeout arrives.
 * 2026-11-19: DIST is disabled. TIM2 is handed to the motor PWM, so the
 * timestamps have no time base; dist_start() replies "DIST err=disabled" and
 * the counter is no longer started. Pins and edge logic are kept as-is so a
 * future time source can re-enable it. */

#define DIST_TRIG_BIT    (1u << 11)
#define DIST_ECHO_BIT    (1u << 10)
#define DIST_TIMEOUT_MS  60u
#define DIST_MIN_US      100u      /* < 2 cm: noise */
#define DIST_MAX_US      25000u    /* > 4.3 m: the module tops out at 4 m */
/* 0 = DIST disabled: its 1 us time base (TIM2) was reassigned to the motor
 * PWM. Set back to 1 only after a different time source is chosen. */
#define DIST_TIME_BASE_READY 0

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

/* One-line summary for the live page's LAST row: "DIST ok 99.0". */
static void dist_last_ok(uint32_t cm10)
{
    char        b[16];
    char       *p = b;
    const char *pre = "DIST ok ";

    while (*pre != '\0') {
        *p++ = *pre++;
    }
    p = pm_cmd_sput_u32(p, cm10 / 10u);
    *p++ = '.';
    *p++ = (char)('0' + (cm10 % 10u));
    *p = '\0';
    pm_cmd_out_last_set(b);
}

static void dist_hw_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /* PA11 push-pull output 2 MHz, PA10 floating input */
    GPIOA->CRH = (GPIOA->CRH & ~((0xFu << 12) | (0xFu << 8)))
               | (0x2u << 12) | (0x4u << 8);
    GPIOA->BRR = DIST_TRIG_BIT;

    /* TIM2 is no longer configured here: it belongs to the motor PWM. DIST is
     * disabled until a new time base is chosen (see dist_start()). */

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

    /* DIST is disabled: TIM2, the 1 us timestamp source, now belongs to the
     * motor PWM. Manual DIST explains the reason; live mode skips silently so
     * the screen is not flooded once per second. */
    if (DIST_TIME_BASE_READY == 0) {
        if (auto_live == 0u) {
            pm_cmd_out_tx_str(c, "DIST err=disabled\r\n");
        }
        return;
    }

    if (s_dist.active != 0u) {
        if (auto_live != 0u) {
            return;                        /* live skips this period */
        }
        /* Manual DIST must not run a second HC-SR04 sequence next to the
         * measurement live started. */
        pm_cmd_out_tx_str(c, (s_dist.auto_live != 0u) ? "DIST err=busy live\r\n"
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
    if (auto_live == 0u) {
        oled_now_row_flash();   /* the echo window is ~20 ms, show it */
    }
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
        pm_cmd_out_last_set("DIST err=tout");
        if (s_dist.auto_live != 0u) {
            pm_cmd_out_tx_str(s_dist.c, "DIST live ");
            s_dist.auto_live = 0u;
        } else {
            pm_cmd_out_tx_str(s_dist.c, "DIST ");
        }
        pm_cmd_out_tx_str(s_dist.c, "err=timeout\r\n");
        return;
    }

    us = (uint32_t)s_dist.width_us;
    s_dist.active = 0u;

    if ((us < DIST_MIN_US) || (us > DIST_MAX_US)) {
        /* HC-SR04 holds ECHO high ~38 ms when nothing reflects the burst. */
        s_dist_last_ok = 0u;
        pm_cmd_out_last_set("DIST err=tout");
        if (s_dist.auto_live != 0u) {
            pm_cmd_out_tx_str(s_dist.c, "DIST live ");
            s_dist.auto_live = 0u;
        } else {
            pm_cmd_out_tx_str(s_dist.c, "DIST ");
        }
        pm_cmd_out_tx_str(s_dist.c, "err=timeout\r\n");
        return;
    }

    cm10 = (us * 10u + 29u) / 58u;         /* us / 58 => cm, kept x10 */

    s_dist_last_cm10 = cm10;
    s_dist_last_ok   = 1u;
    if (s_dist.auto_live == 0u) {
        dist_last_ok(cm10);
    }

    if (s_dist.auto_live != 0u) {
        pm_cmd_out_tx_str(s_dist.c, "DIST live ");
        s_dist.auto_live = 0u;
    } else {
        pm_cmd_out_tx_str(s_dist.c, "DIST ");
    }
    pm_cmd_out_tx_str(s_dist.c, "cm=");
    pm_cmd_out_tx_fixed1(s_dist.c, (int32_t)cm10);
    pm_cmd_out_tx_str(s_dist.c, " us=");
    pm_cmd_out_tx_u32(s_dist.c, us);
    pm_cmd_out_tx_str(s_dist.c, "\r\n");
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

#define OLED_LIVE_PERIOD_MS 500u

static oled_ch1116_t s_oled;

static const char *s_oled_name;
static uint32_t    s_oled_t0;

/* `OLED live on`: refresh the status lines every OLED_LIVE_PERIOD_MS. One job
 * at a time; a period is skipped rather than queued when the panel is busy. */
static uint8_t     s_oled_live;        /* 1 = periodic refresh enabled */
static uint8_t     s_oled_live_job;    /* the running job is a live refresh */
static cmd_port_t *s_oled_live_port;   /* where live refresh replies go */
static uint32_t    s_oled_live_next;   /* ms of the next refresh */
static uint32_t    s_oled_hold_until;  /* non-zero: a static page holds the panel */
static uint32_t    s_oled_last_ms;     /* duration of the last whole-frame push */
static uint8_t     s_oled_silent;      /* one-page NOW repaint: do not reply */

/* Live is the default after boot. A static screen holds the panel for this long
 * by pushing the two next-due timestamps forward; `OLED off` clears live. */
#define OLED_STATIC_HOLD_MS 3000u

static void oled_tx_counters(cmd_port_t *c)
{
    pm_i2c1_stats_t st;

    pm_i2c1_stats(&st);
    pm_cmd_out_tx_str(c, " af=");
    pm_cmd_out_tx_u32(c, st.af);
    pm_cmd_out_tx_str(c, " berr=");
    pm_cmd_out_tx_u32(c, st.berr);
    pm_cmd_out_tx_str(c, " to=");
    pm_cmd_out_tx_u32(c, st.to);
}

static void oled_err_reply(cmd_port_t *c, const char *why)
{
    pm_cmd_out_tx_str(c, "OLED err=");
    pm_cmd_out_tx_str(c, why);
    oled_tx_counters(c);
    pm_cmd_out_tx_str(c, "\r\n");
}

/* Stateless parse/format helpers now live in src/core/pm_cmd.c. */

/* The background job that is running right now, for the NOW row. */
static const char *now_job(void)
{
    if (s_oled_req != NULL) {
        return "OLED PUSH";
    }
    if (aht20_busy() != 0) {
        return "AHT READ";
    }
    if (s_dist.active != 0u) {
        return "DIST MEAS";
    }
    return "IDLE";
}

static const char *oled_mode_name(uint32_t now)
{
    if (s_oled_live == 0u) {
        return "off";
    }
    if ((s_oled_hold_until != 0u) &&
        ((int32_t)(s_oled_hold_until - now) > 0)) {
        return "static";
    }
    return "live";
}

/* The live page, 8 rows x 21 columns. Also used by `OLED show`, so a static
 * text page is the same content the panel shows on its own. */

/* Row 1 only; shared with the short-job repaint below. */
static void oled_stage_now_row(void)
{
    char  b[OLED_CH1116_LINE_CHARS + 1u];
    char *p = pm_cmd_sput_s(b, "NOW : ", 6u);

    p = pm_cmd_sput_s(p, now_job(), 15u);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 1u, b);
}

static void oled_stage_live(void)
{
    char     b[OLED_CH1116_LINE_CHARS + 1u];
    char    *p;
    uint32_t now = pm_osal_now_ms();
    pm_i2c1_stats_t st;
    int32_t  rh10 = 0;
    int32_t  t10  = 0;
    int      have = aht20_reading(&rh10, &t10);

    /* Row 0: MODE (8) + UP (8). */
    p = pm_cmd_sput_s(b, "MODE=", 5u);
    if (s_oled_live == 0u) {
        p = pm_cmd_sput_s(p, "OFF", 8u);
    } else if ((s_oled_hold_until != 0u) &&
               ((int32_t)(s_oled_hold_until - now) > 0)) {
        char     m[9];
        char    *q = m;

        q = pm_cmd_sput_s(q, "STAT", 4u);
        q = pm_cmd_sput_pad(q, ((s_oled_hold_until - now) + 999u) / 1000u, 2u);
        *q++ = 'S';
        *q = '\0';
        p = pm_cmd_sput_s(p, m, 8u);
    } else {
        p = pm_cmd_sput_s(p, "LIVE", 8u);
    }
    p = pm_cmd_sput_s(p, "UP=", 3u);
    p = pm_cmd_sput_pad(p, now / 1000u, 4u);
    *p++ = 'S';
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 0u, b);

    oled_stage_now_row();

    p = pm_cmd_sput_s(b, "LAST: ", 6u);
    p = pm_cmd_sput_s(p, pm_cmd_out_last(), 15u);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 2u, b);

    p = pm_cmd_sput_s(b, "RH=", 3u);
    p = (have != 0) ? pm_cmd_sput_f1(p, rh10) : pm_cmd_sput_s(p, "--.-", 4u);
    p = pm_cmd_sput_s(p, " T=", 3u);
    p = (have != 0) ? pm_cmd_sput_f1(p, t10) : pm_cmd_sput_s(p, "--.-", 4u);
    *p++ = 'C';
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 3u, b);

    adc_cache_read();
    p = pm_cmd_sput_s(b, "NTC=", 4u);
    p = (s_live_adc_ok != 0u) ? pm_cmd_sput_pad(p, s_live_ntc, 4u)
                              : pm_cmd_sput_s(p, "----", 4u);
    p = pm_cmd_sput_s(p, " POT=", 5u);
    p = (s_live_adc_ok != 0u) ? pm_cmd_sput_pad(p, s_live_pot, 4u)
                              : pm_cmd_sput_s(p, "----", 4u);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 4u, b);

    p = pm_cmd_sput_s(b, "K1=", 3u);
    p = pm_cmd_sput_pad(p, s_key_press[0], 5u);
    p = pm_cmd_sput_s(p, " K2=", 4u);
    p = pm_cmd_sput_pad(p, s_key_press[1], 5u);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 5u, b);

    pm_i2c1_stats(&st);
    p = pm_cmd_sput_s(b, "I2C af=", 7u);
    p = pm_cmd_sput_pad(p, st.af, 2u);
    p = pm_cmd_sput_s(p, " berr=", 6u);
    p = pm_cmd_sput_pad(p, st.berr, 2u);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 6u, b);

    p = pm_cmd_sput_s(b, "LIVE ms=", 8u);
    p = (s_oled_last_ms != 0u) ? pm_cmd_sput_pad(p, s_oled_last_ms, 3u)
                               : pm_cmd_sput_s(p, "---", 3u);
    *p = '\0';
    oled_ch1116_stage_line(&s_oled, 7u, b);
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

/* A DIST echo window (~20 ms) or an AHT conversion (80 ms) is far shorter than
 * the 500 ms page period, so a normal frame would almost never show it. Repaint
 * row 1 alone at job start: one page is one I2C transaction (~12 ms), so it
 * still finishes well before the AHT read. Never called from the refresh tick
 * and never in the middle of another panel job. */
static void oled_now_row_flash(void)
{
    uint32_t now = pm_osal_now_ms();

    if ((s_oled_live == 0u) || (s_oled_req != NULL)) {
        return;
    }
    if (s_oled_last != OLED_SCREEN_LIVE) {
        return;                        /* a static page owns the panel */
    }
    if ((s_oled_hold_until != 0u) && ((int32_t)(s_oled_hold_until - now) > 0)) {
        return;
    }
    oled_stage_now_row();
    if (oled_ch1116_start_text(&s_oled, 1u, 1u) != 0) {
        return;
    }
    s_oled_req      = s_oled_live_port;
    s_oled_name     = "now";
    s_oled_t0       = now;
    s_oled_live_job = 1u;
    s_oled_silent   = 1u;
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
        oled_stage_live();
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
    s_oled_last     = OLED_SCREEN_LIVE;
    return 0;
}

/* (Re)start the periodic refresh. `first_ms` is when the first frame is due:
 * boot passes now + one period so the boot clear has finished, while
 * `OLED live on` passes now. The 1 Hz distance measurement sits half a period
 * away from the display refresh. */
static void oled_live_arm(cmd_port_t *port, uint32_t first_ms)
{
    s_oled_live      = 1u;   s_oled_live_port = port;   s_oled_last = OLED_SCREEN_LIVE;
    s_oled_live_next = first_ms;
    s_oled_hold_until = 0u;
    s_dist_live_next = first_ms + (OLED_LIVE_PERIOD_MS / 2u);
}

static void cmd_oled(cmd_port_t *c, const char *arg)
{
    oled_screen_t s = OLED_SCREEN_NONE;
    const char   *rest = NULL;

    /* A pattern command, `show`, `on`/`off` or `live off` takes the panel back
     * even in the middle of a live refresh. Command handling and oled_service()
     * both run in the main loop, so this always lands on a page boundary and no
     * I2C frame is cut in half. */
    if (s_oled_live_job != 0u) {
        int takeover = pm_cmd_ci_equal(arg, "FRAME") || pm_cmd_ci_equal(arg, "TEST") ||
                       pm_cmd_ci_equal(arg, "CLEAR") || pm_cmd_ci_equal(arg, "SHOW") ||
                       pm_cmd_ci_equal(arg, "ON") || pm_cmd_ci_equal(arg, "OFF") ||
                       pm_cmd_ci_starts(arg, "LIVE ", &rest) ||
                       pm_cmd_ci_starts(arg, "COL", &rest);

        if (takeover != 0) {
            oled_ch1116_abort(&s_oled);
            s_oled_req      = NULL;
            s_oled_live_job = 0u;
            s_oled_silent   = 0u;
        }
    }

    /* Text dump of the page: no I2C, so it is allowed even mid-push and is the
     * only way to read MODE/STATIC/OFF back while the panel is blank or shows
     * a static pattern. */
    if (pm_cmd_ci_equal(arg, "DUMP")) {
        uint32_t now = pm_osal_now_ms();
        uint8_t  pg;

        pm_cmd_out_tx_str(c, "OLED dump mode=");
        pm_cmd_out_tx_str(c, oled_mode_name(now));
        if ((s_oled_live != 0u) && (s_oled_hold_until != 0u) &&
            ((int32_t)(s_oled_hold_until - now) > 0)) {
            pm_cmd_out_tx_str(c, " left=");
            pm_cmd_out_tx_u32(c, ((s_oled_hold_until - now) + 999u) / 1000u);
            pm_cmd_out_tx_str(c, "s");
        }
        pm_cmd_out_tx_str(c, " screen=");
        pm_cmd_out_tx_str(c, oled_screen_name(s_oled_last));
        pm_cmd_out_tx_str(c, "\r\n");
        if ((s_oled_last == OLED_SCREEN_LIVE) ||
            (s_oled_last == OLED_SCREEN_SHOW)) {
            for (pg = 0u; pg < (uint8_t)OLED_CH1116_PAGES; pg++) {
                pm_cmd_out_tx_str(c, "OLED L");
                pm_cmd_out_tx_u32(c, pg);
                pm_cmd_out_tx_str(c, "=");
                pm_cmd_out_tx_str(c, s_oled.line[pg]);
                pm_cmd_out_tx_str(c, "\r\n");
            }
        }
        pm_cmd_out_last_set("OLED dump");
        return;
    }

    if (s_oled_req != NULL) {
        pm_cmd_out_tx_str(c, "OLED err=busy pending\r\n");
        return;
    }
    /* AHTS keeps bus ownership across its 80 ms window; do not cut in. */
    if (aht20_busy() != 0) {
        pm_cmd_out_tx_str(c, "OLED err=busy i2c\r\n");
        return;
    }

    if (pm_cmd_ci_equal(arg, "ON")) {
        if (oled_ch1116_display(&s_oled, 1) != 0) {
            oled_err_reply(c, "io");
            return;
        }
        oled_live_arm(c, pm_osal_now_ms());
        pm_cmd_out_last_set("OLED on ok");
        pm_cmd_out_tx_str(c, "OLED on ok display=on live=on");
        oled_tx_counters(c);
        pm_cmd_out_tx_str(c, "\r\n");
        return;
    }
    if (pm_cmd_ci_equal(arg, "OFF")) {
        if (oled_ch1116_display(&s_oled, 0) != 0) {
            oled_err_reply(c, "io");
            return;
        }
        s_oled_live = 0u;   /* stop refreshing and stop the 1 Hz ranging */
        s_oled_hold_until = 0u;
        pm_cmd_out_last_set("OLED off ok");
        pm_cmd_out_tx_str(c, "OLED off ok display=off live=paused");
        oled_tx_counters(c);
        pm_cmd_out_tx_str(c, "\r\n");
        return;
    }

    /* `OLED live on|off`; a bare `OLED live` is a usage error. */
    if (pm_cmd_ci_equal(arg, "LIVE")) {
        pm_cmd_out_tx_str(c, "OLED err=usage\r\n");
        return;
    }
    if (pm_cmd_ci_starts(arg, "LIVE ", &rest)) {
        while (*rest == ' ') {
            rest++;
        }
        if (pm_cmd_ci_equal(rest, "ON")) {
            oled_live_arm(c, pm_osal_now_ms());
            pm_cmd_out_tx_str(c, "OLED live=on period=");
            pm_cmd_out_tx_u32(c, OLED_LIVE_PERIOD_MS);
            pm_cmd_out_tx_str(c, "ms\r\n");
            return;
        }
        if (pm_cmd_ci_equal(rest, "OFF")) {
            s_oled_live = 0u;
            pm_cmd_out_tx_str(c, "OLED live=off\r\n");
            return;
        }
        pm_cmd_out_tx_str(c, "OLED err=usage\r\n");
        return;
    }

    /* Diagnostic for the 132-column RAM / 128-column glass offset. Repaints the
     * last screen so the A/B needs no extra `frame` command. */
    if (pm_cmd_ci_starts(arg, "COL", &rest)) {
        uint32_t v;

        if ((pm_cmd_parse_u32(rest, &v) == 0) || (v > 4u)) {
            pm_cmd_out_tx_str(c, "OLED err=usage col=0..4\r\n");
            return;
        }
        oled_ch1116_set_col_offset(&s_oled, (uint8_t)v);
        pm_cmd_out_tx_str(c, "OLED col=");
        pm_cmd_out_tx_u32(c, v);
        pm_cmd_out_tx_str(c, " ok");
        if (s_oled_last == OLED_SCREEN_LIVE) {
            uint32_t now = pm_osal_now_ms();

            if (oled_start_live_job(c, "redraw", now) == 0) {
                s_oled_live_next = now + OLED_LIVE_PERIOD_MS;
                pm_cmd_out_tx_str(c, " redraw=live");
            }
        } else if (s_oled_last != OLED_SCREEN_NONE) {
            if (oled_start_screen(s_oled_last) == 0) {
                s_oled_req  = c;
                s_oled_name = "redraw";
                s_oled_t0   = pm_osal_now_ms();
                pm_cmd_out_tx_str(c, " redraw=");
                pm_cmd_out_tx_str(c, oled_screen_name(s_oled_last));
            }
        }
        pm_cmd_out_tx_str(c, "\r\n");
        return;
    }

    if (pm_cmd_ci_equal(arg, "CLEAR")) {
        s = OLED_SCREEN_CLEAR;
    } else if (pm_cmd_ci_equal(arg, "TEST")) {
        s = OLED_SCREEN_TEST;
    } else if (pm_cmd_ci_equal(arg, "FRAME")) {
        s = OLED_SCREEN_FRAME;
    } else if (pm_cmd_ci_equal(arg, "SHOW")) {
        s = OLED_SCREEN_SHOW;
    } else {
        pm_cmd_out_tx_str(c, "OLED err=usage\r\n");
        return;
    }

    /* Static screens take the panel for OLED_STATIC_HOLD_MS and then hand it
     * back to live; it is only a temporary takeover, live stays armed. */
    if (oled_start_screen(s) != 0) {
        pm_cmd_out_tx_str(c, "OLED err=busy pending\r\n");
        return;
    }

    s_oled_last  = s;
    s_oled_req   = c;
    s_oled_name  = oled_screen_name(s);
    s_oled_t0    = pm_osal_now_ms();
    pm_cmd_out_last_two("OLED ", oled_screen_name(s));
}

/* Called once per main loop iteration: at most one page per call. */
static void oled_service(void)
{
    cmd_port_t *c;
    int         was_live;
    uint32_t    ms;

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
    ms          = (uint32_t)(pm_osal_now_ms() - s_oled_t0);

    if (s_oled_silent != 0u) {
        s_oled_silent = 0u;
        if (oled_ch1116_err(&s_oled) != 0) {
            oled_ch1116_abort(&s_oled);
        }
        return;                        /* internal NOW repaint, not a reply */
    }
    if (oled_ch1116_err(&s_oled) != 0) {
        oled_err_reply(c, "io");
        return;
    }
    if (was_live != 0) {
        s_oled_last_ms = ms;           /* health value shown as "LIVE ms=" */
    }
    pm_cmd_out_tx_str(c, "OLED ");
    pm_cmd_out_tx_str(c, s_oled_name);
    pm_cmd_out_tx_str(c, " ok ms=");
    pm_cmd_out_tx_u32(c, ms);
    pm_cmd_out_tx_str(c, " col=");
    pm_cmd_out_tx_u32(c, (uint32_t)oled_ch1116_col_offset(&s_oled));
    /* A periodic refresh already implies live=on; pattern commands report that
     * they took the panel back. */
    if (was_live == 0) {
        pm_cmd_out_tx_str(c, " live=");
        pm_cmd_out_tx_str(c, (s_oled_live != 0u) ? "on" : "off");
        /* A static screen only holds the panel briefly before live returns. */
        if (s_oled_live != 0u) {
            s_oled_live_next = pm_osal_now_ms() + OLED_STATIC_HOLD_MS;
            s_oled_hold_until = s_oled_live_next;
            s_dist_live_next = s_oled_live_next + (OLED_LIVE_PERIOD_MS / 2u);
            pm_cmd_out_tx_str(c, " hold="); pm_cmd_out_tx_u32(c, OLED_STATIC_HOLD_MS); pm_cmd_out_tx_str(c, "ms");
        }
    }
    oled_tx_counters(c);
    pm_cmd_out_tx_str(c, "\r\n");
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
    if (aht20_busy() != 0) {
        return;                        /* do not interleave with the AHT window */
    }
    s_oled_live_next = now + OLED_LIVE_PERIOD_MS;
    s_oled_hold_until = 0u;            /* the static page's hold has expired */
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

/* Command registry. Entries are in HELP output order. Routing precedence is
 * the old if/else-if order: names first, then the five device consumers, then
 * the "OLED <arg>" prefix. pm_cmd_dispatch() walks the table three times, one
 * pass per kind, so declaration order only decides within a single kind. */
static void cmd_help(cmd_port_t *c, const char *line);
static void cmd_ping(cmd_port_t *c, const char *line);
static void cmd_stat(cmd_port_t *c, const char *line);
static void cmd_keys(cmd_port_t *c, const char *line);

static void run_i2c(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    cmd_i2c(c);
}

static void run_aht(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    aht20_start(c, 0);
}

static void run_ahts(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    aht20_start(c, 1);
}

static void run_dist(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    dist_start(c, 0u);
}

static void run_adc(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    cmd_adc(c);
}

static void run_oled(cmd_port_t *c, const char *arg)
{
    cmd_oled(c, arg);
}

static const pm_cmd_entry_t s_cmds[] = {
    { "HELP",  "HELP=list commands",                              PM_CMD_EXACT,  cmd_help,  NULL, NULL, NULL },
    { "PING",  "PING=reply PONG",                                 PM_CMD_EXACT,  cmd_ping,  NULL, NULL, NULL },
    { "STAT",  "STAT=clk/up/tick/keys/uart counters",             PM_CMD_EXACT,  cmd_stat,  NULL, NULL, NULL },
    { "I2C",   "I2C=scan I2C1 bus, list 8-bit addresses",         PM_CMD_EXACT,  run_i2c,   NULL, NULL, NULL },
    { "AHT",   "AHT=read AHT20, rh and t in 0.1 units",           PM_CMD_EXACT,  run_aht,   NULL, NULL, NULL },
    { "AHTS",  "AHTS=same but read via repeated START",           PM_CMD_EXACT,  run_ahts,  NULL, NULL, NULL },
    { "DIST",  "DIST=disabled, TIM2 given to motor",              PM_CMD_EXACT,  run_dist,  NULL, NULL, NULL },
    { "ADC",   "ADC=pot PA5 and NTC PA4, raw and mV",             PM_CMD_EXACT,  run_adc,   NULL, NULL, NULL },
    { "OLED",  "OLED=on/off/clear/test/show/frame/col/live/dump", PM_CMD_PREFIX, NULL, NULL, oled_arg, run_oled },
    { "PWM",   "PWM=servo 0..180 deg, buzzer 100..5000 Hz, sweep", PM_CMD_TRY,   NULL, pwm_dev_cmd,     NULL, NULL },
    { "TRACK", "TRACK=raw level of PB14 line sensor",             PM_CMD_TRY,    NULL, track_dev_cmd,   NULL, NULL },
    { "ENC",   "ENC=count/clear, dir, a/b, button PB15",          PM_CMD_TRY,    NULL, encoder_dev_cmd, NULL, NULL },
    { "RELAY", "RELAY=on/off/stat, PB5 relay coil",               PM_CMD_TRY,    NULL, relay_dev_cmd,   NULL, NULL },
    { "MOTOR", "MOTOR=fwd/rev/stop/duty/stat, PA0/PA1 TIM2 PWM",  PM_CMD_TRY,    NULL, motor_dev_cmd,   NULL, NULL },
    { "KEYS",  "KEYS=key press counters, clears them",            PM_CMD_EXACT,  cmd_keys,  NULL, NULL, NULL },
};

static void cmd_help(cmd_port_t *c, const char *line)
{
    char   buf[80];
    size_t i;
    size_t n = sizeof(s_cmds) / sizeof(s_cmds[0]);

    PM_UNUSED(line);
    for (i = 0u; i < n; i++) {
        if (pm_cmd_help_line(s_cmds, n, i, buf, sizeof(buf)) == 0) {
            pm_cmd_out_tx_str(c, buf);
        }
    }
}

static void cmd_ping(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    pm_cmd_out_tx_str(c, "PONG\r\n");
}

static void cmd_stat(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    pm_cmd_out_tx_str(c, "STAT clk=");
    pm_cmd_out_tx_u32(c, pm_platform_core_clock());
    pm_cmd_out_tx_str(c, " up=");
    pm_cmd_out_tx_u32(c, pm_osal_now_ms());
    pm_cmd_out_tx_str(c, " tick=1 key1=");
    pm_cmd_out_tx_u32(c, s_key_press[0]);
    pm_cmd_out_tx_str(c, " key2=");
    pm_cmd_out_tx_u32(c, s_key_press[1]);
    pm_cmd_out_tx_str(c, " uart_rx=");
    pm_cmd_out_tx_u32(c, s_rx_total);
    pm_cmd_out_tx_str(c, " uart_tx=");
    pm_cmd_out_tx_u32(c, pm_cmd_out_tx_total());
    pm_cmd_out_tx_str(c, "\r\n");
}

static void cmd_keys(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    pm_cmd_out_tx_str(c, "KEYS k1=");
    pm_cmd_out_tx_u32(c, s_key_press[0]);
    pm_cmd_out_tx_str(c, " k2=");
    pm_cmd_out_tx_u32(c, s_key_press[1]);
    pm_cmd_out_tx_str(c, " last=");
    pm_cmd_out_tx_str(c, (s_key_last == 0) ? "k1" : (s_key_last == 1) ? "k2" : "none");
    pm_cmd_out_tx_str(c, "\r\n");
    s_key_press[0] = 0u;
    s_key_press[1] = 0u;
    s_key_last = -1;
}

static void cmd_execute(cmd_port_t *c, const char *line)
{
    pm_cmd_dispatch(s_cmds, sizeof(s_cmds) / sizeof(s_cmds[0]), c, line);
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

    if (oled_ch1116_init(&s_oled, pm_i2c1_ops()->write, NULL) != 0) {
        pm_cmd_out_uart_write_str("OLED CH1116 addr=7A init FAILED\r\n");
        return;
    }
    pm_cmd_out_uart_write_str("OLED CH1116 addr=7A init ok, col=");
    pm_cmd_out_uart_write_u32((uint32_t)oled_ch1116_col_offset(&s_oled));
    pm_cmd_out_uart_write_str(", clearing\r\n");
    (void)oled_ch1116_start_pattern(&s_oled, 0x00u);
    while ((oled_ch1116_service(&s_oled) != 0) &&
           (guard < (uint8_t)OLED_CH1116_PAGES)) {
        guard++;
    }
    (void)oled_ch1116_display(&s_oled, 1);
    if (oled_ch1116_err(&s_oled) != 0) {
        pm_cmd_out_uart_write_str("OLED clear FAILED\r\n");
        return;
    }
    /* The boot clear is a real screen state: `OLED col n` repaints it so an
     * A/B before the first pattern command still shows a redraw. */
    s_oled_last = OLED_SCREEN_CLEAR;
    pm_cmd_out_uart_write_str("OLED ready (128x64, 6x8 font)\r\n");
}

static void bt_init(void)
{
    if (pm_uart_init(&s_bt, &s_bt_cfg) == PM_OK) {
        pm_cmd_out_uart_write_str("BT USART3 9600 8N1, command channel ready\r\n");
    } else {
        pm_cmd_out_uart_write_str("BT USART3 init FAILED\r\n");
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
        pm_cmd_out_uart_write_str("BT rx n=");
        pm_cmd_out_uart_write_u32((uint32_t)n);
        pm_cmd_out_uart_write_str("B:");
        pm_cmd_out_uart_write_hex(buf, n);
        pm_cmd_out_uart_write_str("\r\n");
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
    (void)pm_osal_init();   /* the 1 ms tick belongs to the entry layer, not to the board */

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
    pm_cmd_out_init(&s_uart, &s_bt);

    pm_cmd_out_uart_write_str("\r\nboard-bringup\r\ncore clock = ");
    pm_cmd_out_uart_write_u32(pm_platform_core_clock());
    pm_cmd_out_uart_write_str(" Hz\r\nUSART2 115200 8N1, command channel ready\r\n");

    bt_init();
    dist_hw_init();
    adc_boot();
    oled_boot();
    aht20_init(&s_aht_hooks);
    pwm_dev_init();
    track_dev_init(); encoder_dev_init(); relay_dev_init(); motor_dev_init();

    /* Boot default: live. Armed only after oled_boot() has finished the init
     * sequence and the synchronous clear, so the first refresh cannot race the
     * clear. One period of delay leaves the boot messages on screen briefly. */
    oled_live_arm(&s_debug, pm_osal_now_ms() + OLED_LIVE_PERIOD_MS);

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
        aht20_bg_tick(t);
        aht20_service(t);
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
            pm_cmd_out_uart_write_str("hb\r\n");
        }
    }
}
