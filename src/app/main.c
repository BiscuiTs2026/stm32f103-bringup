/*
 * main.c - entry glue for the STM32F103C8T6 board bring-up.
 *
 * This file only sequences initialization and schedules work. It contains no
 * driver code and no register access; everything goes through pm_* APIs. The
 * command table and its adapters live in src/app/app_wiring.c.
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
#include "pm_cmd_out.h"
#include "pm_cmd_console.h"
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
#include "app_wiring.h"

static const pm_gpio_cfg_t s_led_cfg = {
    .dir           = PM_GPIO_DIR_OUT,
    .pull          = PM_GPIO_PULL_NONE,
    .speed         = PM_GPIO_SPEED_LOW,
    .initial_level = PM_GPIO_LEVEL_LOW,
};

/* Console transport (UARTs, framing) lives in src/components/pm_cmd; the entry
 * layer installs the executor from src/app/app_wiring.c here. */
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
    s_console.execute = app_cmd_execute;
    if (pm_cmd_console_init(&s_console, BOARD_UART_DEBUG, BOARD_UART_BT) != 0) {
        error_loop();
    }
    pm_cmd_out_init(s_console.debug_uart, s_console.bt_uart);

    pm_cmd_out_uart_write_str("\r\nboard-bringup\r\ncore clock = ");
    pm_cmd_out_uart_write_u32(pm_platform_core_clock());
    pm_cmd_out_uart_write_str(" Hz\r\nUSART2 115200 8N1, command channel ready\r\n");

    pm_cmd_console_init_bt();
    pm_sonar_init();
    adc_dev_boot();
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

        /* Fresh timestamp: the console feed stamps last_ms and aht20_start()
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
