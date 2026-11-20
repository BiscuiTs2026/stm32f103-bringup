/*
 * sys_cmds.c - HELP / PING / STAT / I2C replies, moved out of src/app/main.c
 * (split step S10).
 *
 * Reply text and field order are unchanged from the entry-glue version. The
 * I2C command only formats what the bsp scan returned; the polling master
 * stays in src/port/stm32f1/pm_i2c1_stm32f1.c.
 */
#include "sys_cmds.h"

#include "pm_cmd_console.h"
#include "pm_osal.h"
#include "pm_platform.h"
#include "pm_i2c1.h"
#include "keypad_dev.h"

void sys_cmd_help(cmd_port_t *c, const pm_cmd_entry_t *t, size_t n)
{
    char   buf[80];
    size_t i;

    for (i = 0u; i < n; i++) {
        if (pm_cmd_help_line(t, n, i, buf, sizeof(buf)) == 0) {
            pm_cmd_out_tx_str(c, buf);
        }
    }
}

void sys_cmd_ping(cmd_port_t *c)
{
    pm_cmd_out_tx_str(c, "PONG\r\n");
}

void sys_cmd_stat(cmd_port_t *c)
{
    uint32_t k1;
    uint32_t k2;

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

void sys_cmd_i2c(cmd_port_t *c)
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
