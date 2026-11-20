/*
 * app_wiring.c - command table and command adapters, moved out of src/app/main.c
 * (split step S10).
 *
 * Entries are in HELP output order. Routing precedence is the old if/else-if
 * order: names first, then the five device consumers, then the "OLED <arg>"
 * prefix. pm_cmd_dispatch() walks the table three times, one pass per kind, so
 * declaration order only decides within a single kind.
 */
#include "app_wiring.h"

#include "pm_common.h"
#include "pm_cmd_table.h"
#include "sys_cmds.h"
#include "aht20_dev.h"
#include "adc_dev.h"
#include "sonar_dev.h"
#include "oled_ui.h"
#include "keypad_dev.h"
#include "pwm_dev.h"
#include "track_dev.h"
#include "encoder_dev.h"
#include "relay_dev.h"
#include "motor_dev.h"

static void run_help(cmd_port_t *c, const char *line);
static void run_ping(cmd_port_t *c, const char *line);
static void run_stat(cmd_port_t *c, const char *line);
static void run_keys(cmd_port_t *c, const char *line);
static void run_i2c(cmd_port_t *c, const char *line);
static void run_aht(cmd_port_t *c, const char *line);
static void run_ahts(cmd_port_t *c, const char *line);
static void run_dist(cmd_port_t *c, const char *line);
static void run_adc(cmd_port_t *c, const char *line);
static void run_oled(cmd_port_t *c, const char *arg);

static const pm_cmd_entry_t s_cmds[] = {
    { "HELP",  "HELP=list commands",                              PM_CMD_EXACT,  run_help,  NULL, NULL, NULL },
    { "PING",  "PING=reply PONG",                                 PM_CMD_EXACT,  run_ping,  NULL, NULL, NULL },
    { "STAT",  "STAT=clk/up/tick/keys/uart counters",             PM_CMD_EXACT,  run_stat,  NULL, NULL, NULL },
    { "I2C",   "I2C=scan I2C1 bus, list 8-bit addresses",         PM_CMD_EXACT,  run_i2c,   NULL, NULL, NULL },
    { "AHT",   "AHT=read AHT20, rh and t in 0.1 units",           PM_CMD_EXACT,  run_aht,   NULL, NULL, NULL },
    { "AHTS",  "AHTS=same but read via repeated START",           PM_CMD_EXACT,  run_ahts,  NULL, NULL, NULL },
#if PM_ENABLE_SONAR
    { "DIST",  "DIST=HC-SR04 PA11/PA10, cm/us (DWT base)",        PM_CMD_EXACT,  run_dist,  NULL, NULL, NULL },
#else
    { "DIST",  "DIST=disabled, TIM2 given to motor",              PM_CMD_EXACT,  run_dist,  NULL, NULL, NULL },
#endif
    { "ADC",   "ADC=pot PA5 and NTC PA4, raw and mV",             PM_CMD_EXACT,  run_adc,   NULL, NULL, NULL },
    { "OLED",  "OLED=on/off/clear/test/show/frame/col/live/dump", PM_CMD_PREFIX, NULL, NULL, oled_ui_arg, run_oled },
    { "PWM",   "PWM=servo 0..180 deg, buzzer 100..5000 Hz, sweep", PM_CMD_TRY,   NULL, pwm_dev_cmd,     NULL, NULL },
    { "TRACK", "TRACK=raw level of PB14 line sensor",             PM_CMD_TRY,    NULL, track_dev_cmd,   NULL, NULL },
    { "ENC",   "ENC=count/clear, dir, a/b, button PB15",          PM_CMD_TRY,    NULL, encoder_dev_cmd, NULL, NULL },
    { "RELAY", "RELAY=on/off/stat, PB5 relay coil",               PM_CMD_TRY,    NULL, relay_dev_cmd,   NULL, NULL },
    { "MOTOR", "MOTOR=fwd/rev/stop/brake/duty/stat, PA0/PA1 TIM2 PWM", PM_CMD_TRY, NULL, motor_dev_cmd,   NULL, NULL },
    { "KEYS",  "KEYS=key press counters, clears them",            PM_CMD_EXACT,  run_keys,  NULL, NULL, NULL },
};

static void run_help(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    sys_cmd_help(c, s_cmds, sizeof(s_cmds) / sizeof(s_cmds[0]));
}

static void run_ping(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    sys_cmd_ping(c);
}

static void run_stat(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    sys_cmd_stat(c);
}

static void run_keys(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    keypad_dev_cmd(c);
}

static void run_i2c(cmd_port_t *c, const char *line)
{
    PM_UNUSED(line);
    sys_cmd_i2c(c);
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
    adc_dev_cmd(c);   // arch-allow: component call, adc_ prefix is not an SPL call
}

static void run_oled(cmd_port_t *c, const char *arg)
{
    oled_ui_cmd(c, arg);
}

void app_cmd_execute(cmd_port_t *c, const char *line)
{
    pm_cmd_dispatch(s_cmds, sizeof(s_cmds) / sizeof(s_cmds[0]), c, line);
}
