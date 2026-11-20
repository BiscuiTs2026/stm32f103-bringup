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
#include "pm_cmd_console.h"
#include "pm_i2c1.h"
#include "pm_adc1.h"
#include "pm_sonar.h"
#include "sonar_dev.h"
#include "adc_dev.h"
#include "pm_platform.h"
#include "board_f103c8.h"
#include "oled_ui.h"
#include "aht20_dev.h"
#include "keypad_dev.h"
#include "pm_keypad.h"
#include "pwm_dev.h"
#include "track_dev.h"
#include "encoder_dev.h"
#include "relay_dev.h"
#include "motor_dev.h"

#include <string.h>

static const pm_gpio_cfg_t s_led_cfg = {
    .dir           = PM_GPIO_DIR_OUT,
    .pull          = PM_GPIO_PULL_NONE,
    .speed         = PM_GPIO_SPEED_LOW,
    .initial_level = PM_GPIO_LEVEL_LOW,
};

/* Console transport (UARTs, framing) lives in src/components/pm_cmd; the entry
 * layer keeps the command table and installs its executor here. */
static pm_cmd_console_t s_console;

/* The AHT20 job lives in src/components/aht20_dev; these two hooks are the
 * only things it needs from the entry layer. */
static const aht20_hooks_t s_aht_hooks = {
    .ui_busy    = oled_ui_busy,
    .ui_now_row = oled_ui_now_row,
};

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

/* ADC1 init lives in src/port/stm32f1/pm_adc1_stm32f1.c; the banner texts
 * stay here so the entry layer keeps its power-up output. */
static void adc_boot(void)
{
    int rc = pm_adc1_init();

    if (rc == 1) {
        pm_cmd_out_uart_write_str("ADC1 init FAILED\r\n");
        return;
    }
    if (rc == 2) {
        pm_cmd_out_uart_write_str("ADC1 calibration FAILED\r\n");
        return;
    }
    pm_cmd_out_uart_write_str("ADC1 PA4=IN4 ntc, PA5=IN5 pot, 12-bit\r\n");
}

/* ---------------------------------------------- HC-SR04 distance, PA11/PA10
 * The job (DIST command, reply formatting, last-good value) lives in
 * src/components/sonar_dev; the pins, the EXTI10 handler and the TIM2 time
 * stamp live in src/port/stm32f1/pm_sonar_stm32f1.c. This layer only wires
 * them to the command table and the main loop. */

/* The screen component reaches the measurement job through two hooks; both
 * are thin adapters over sonar_dev. */
static int oled_ui_dist_busy(void)
{
    return sonar_dev_busy();
}

static void oled_ui_dist_poke(cmd_port_t *port)
{
    sonar_dev_live_poke(port);
}

static const oled_ui_hooks_t s_oled_hooks = {
    .dist_busy      = oled_ui_dist_busy,
    .dist_live_poke = oled_ui_dist_poke,
};

/* Command registry. Entries are in HELP output order. Routing precedence is
 * the old if/else-if order: names first, then the five device consumers, then
 * the "OLED <arg>" prefix. pm_cmd_dispatch() walks the table three times, one
 * pass per kind, so declaration order only decides within a single kind. */
static void cmd_help(cmd_port_t *c, const char *line);
static void cmd_ping(cmd_port_t *c, const char *line);
static void cmd_stat(cmd_port_t *c, const char *line);
static void run_keys(cmd_port_t *c, const char *line);

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
    sonar_dev_start(c, 0u);
}

static void run_adc(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    adc_dev_cmd(c);
}

static void run_oled(cmd_port_t *c, const char *arg)
{
    oled_ui_cmd(c, arg);
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
    { "OLED",  "OLED=on/off/clear/test/show/frame/col/live/dump", PM_CMD_PREFIX, NULL, NULL, oled_ui_arg, run_oled },
    { "PWM",   "PWM=servo 0..180 deg, buzzer 100..5000 Hz, sweep", PM_CMD_TRY,   NULL, pwm_dev_cmd,     NULL, NULL },
    { "TRACK", "TRACK=raw level of PB14 line sensor",             PM_CMD_TRY,    NULL, track_dev_cmd,   NULL, NULL },
    { "ENC",   "ENC=count/clear, dir, a/b, button PB15",          PM_CMD_TRY,    NULL, encoder_dev_cmd, NULL, NULL },
    { "RELAY", "RELAY=on/off/stat, PB5 relay coil",               PM_CMD_TRY,    NULL, relay_dev_cmd,   NULL, NULL },
    { "MOTOR", "MOTOR=fwd/rev/stop/duty/stat, PA0/PA1 TIM2 PWM",  PM_CMD_TRY,    NULL, motor_dev_cmd,   NULL, NULL },
    { "KEYS",  "KEYS=key press counters, clears them",            PM_CMD_EXACT,  run_keys,  NULL, NULL, NULL },
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
    uint32_t k1;
    uint32_t k2;

    PM_UNUSED(line);
    keypad_dev_counts(&k1, &k2);
    pm_cmd_out_tx_str(c, "STAT clk=");
    pm_cmd_out_tx_u32(c, pm_platform_core_clock());
    pm_cmd_out_tx_str(c, " up=");
    pm_cmd_out_tx_u32(c, pm_osal_now_ms());
    pm_cmd_out_tx_str(c, " tick=1 key1=");
    pm_cmd_out_tx_u32(c, k1);
    pm_cmd_out_tx_str(c, " key2=");
    pm_cmd_out_tx_u32(c, k2);
    pm_cmd_out_tx_str(c, " uart_rx=");
    pm_cmd_out_tx_u32(c, pm_cmd_console_rx_total());
    pm_cmd_out_tx_str(c, " uart_tx=");
    pm_cmd_out_tx_u32(c, pm_cmd_out_tx_total());
    pm_cmd_out_tx_str(c, "\r\n");
}

static void run_keys(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    keypad_dev_cmd(c);
}

static void cmd_execute(cmd_port_t *c, const char *line)
{
    pm_cmd_dispatch(s_cmds, sizeof(s_cmds) / sizeof(s_cmds[0]), c, line);
}

int main(void)
{
    pm_gpio_pin_t led_blue = { .id = BOARD_GPIO_LED_BLUE };
    const pm_keypad_map_t *key_map;
    size_t   key_count;
    uint32_t next_led_ms;
    uint32_t next_hb_ms;

    pm_platform_init();
    (void)pm_osal_init();   /* the 1 ms tick belongs to the entry layer, not to the board */

    if (pm_gpio_configure(led_blue, &s_led_cfg) != PM_OK) {
        error_loop();
    }
    key_map = pm_keypad_map(&key_count);
    if ((key_map == NULL) || (keypad_dev_init(key_map, key_count) != 0)) {
        error_loop();
    }
    s_console.execute = cmd_execute;
    if (pm_cmd_console_init(&s_console, BOARD_UART_DEBUG, BOARD_UART_BT) != 0) {
        error_loop();
    }
    pm_cmd_out_init(s_console.debug_uart, s_console.bt_uart);

    pm_cmd_out_uart_write_str("\r\nboard-bringup\r\ncore clock = ");
    pm_cmd_out_uart_write_u32(pm_platform_core_clock());
    pm_cmd_out_uart_write_str(" Hz\r\nUSART2 115200 8N1, command channel ready\r\n");

    pm_cmd_console_init_bt();
    pm_sonar_init();
    adc_boot();
    oled_ui_boot(&s_oled_hooks, s_console.debug_port);
    aht20_init(&s_aht_hooks);
    pwm_dev_init();
    track_dev_init(); encoder_dev_init(); relay_dev_init(); motor_dev_init();

    next_led_ms = pm_osal_now_ms();
    next_hb_ms  = next_led_ms;

    for (;;) {
        uint32_t now = pm_osal_now_ms();
        uint32_t t;

        pm_cmd_console_service_debug();
        keypad_dev_service(now);
        pm_cmd_console_service_bt();

        /* Fresh timestamp: the console feed stamps last_ms and aht_start()
         * stamps t0 *after* `now` was sampled. Comparing against the stale
         * `now` makes (now - last_ms) wrap to a huge value, which fired the
         * idle flush and the AHT window immediately. */
        t = pm_osal_now_ms();
        pm_cmd_console_flush_idle(t);
        aht20_bg_tick(t);
        aht20_service(t);
        sonar_dev_service(t);
        oled_ui_service();
        oled_ui_live_tick(pm_osal_now_ms());

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
