/*
 * keypad_dev.c - debounced keys with LED mirror and press counters, moved out
 * of src/app/main.c (split step S8).
 *
 * Behaviour is unchanged from the entry-layer version: 20 ms debounce, count
 * and `last` update on the press edge only, LED follows the debounced state,
 * and one line is reported per edge through pm_cmd_out_uart_write_str().
 */
#include "keypad_dev.h"

#define KEY_DEBOUNCE_MS 20u

/* Same values as the entry layer's output config; kept local so the component
 * owns its LEDs and main.c keeps its own s_led_cfg for the blue heartbeat. */
static const pm_gpio_cfg_t s_led_out_cfg = {
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

typedef struct {
    pm_gpio_pin_t key;
    pm_gpio_pin_t led;
    const char   *name;
    uint8_t       stable;      /* 1 = pressed */
    uint8_t       raw;
    uint32_t      raw_since;
} key_state_t;

static key_state_t s_keys[KEYPAD_MAX_KEYS];
static size_t      s_count;
static uint32_t    s_press[KEYPAD_MAX_KEYS];
static int         s_last = -1;

int keypad_dev_init(const pm_keypad_map_t *map, size_t n)
{
    size_t i;

    if ((map == NULL) || (n == 0u) || (n > KEYPAD_MAX_KEYS)) {
        return -1;
    }
    for (i = 0u; i < n; i++) {
        if (pm_gpio_configure(map[i].key, &s_key_cfg) != PM_OK) {
            return -1;
        }
        if (pm_gpio_configure(map[i].led, &s_led_out_cfg) != PM_OK) {
            return -1;
        }
        s_keys[i].key  = map[i].key;
        s_keys[i].led  = map[i].led;
        s_keys[i].name = map[i].name;
    }
    s_count = n;
    return 0;
}

void keypad_dev_service(uint32_t now)
{
    size_t i;

    for (i = 0u; i < s_count; i++) {
        key_state_t *k = &s_keys[i];
        pm_gpio_level_t level = PM_GPIO_LEVEL_HIGH;
        uint8_t pressed;

        if (pm_gpio_read(k->key, &level) != PM_OK) {
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
                s_press[i]++;
                s_last = (int)i;
            }
            (void)pm_gpio_write(k->led,
                                (pressed != 0u) ? PM_GPIO_LEVEL_HIGH
                                                : PM_GPIO_LEVEL_LOW);
            pm_cmd_out_uart_write_str(k->name);
            pm_cmd_out_uart_write_str((pressed != 0u) ? " pressed\r\n" : " released\r\n");
        }
    }
}

void keypad_dev_cmd(cmd_port_t *c)
{
    pm_cmd_out_tx_str(c, "KEYS k1=");
    pm_cmd_out_tx_u32(c, s_press[0]);
    pm_cmd_out_tx_str(c, " k2=");
    pm_cmd_out_tx_u32(c, s_press[1]);
    pm_cmd_out_tx_str(c, " last=");
    pm_cmd_out_tx_str(c, (s_last == 0) ? "k1" : (s_last == 1) ? "k2" : "none");
    pm_cmd_out_tx_str(c, "\r\n");
    s_press[0] = 0u;
    s_press[1] = 0u;
    s_last = -1;
}

void keypad_dev_counts(uint32_t *k1, uint32_t *k2)
{
    *k1 = (s_count > 0u) ? s_press[0] : 0u;
    *k2 = (s_count > 1u) ? s_press[1] : 0u;
}
