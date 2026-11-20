/*
 * adc_dev.c - ADC command and live-page sampling, moved out of src/app/main.c
 * (split step S7b).
 *
 * Sampling itself is pm_adc1_sample(); this file owns the reply formatting and
 * the "sample the two channels in this order" policy. There is no background
 * timer on purpose: the page samples while it paints, as before.
 */
#include "adc_dev.h"

#include "pm_adc1.h"

void adc_dev_boot(void)
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

uint32_t adc_dev_to_mv(uint16_t raw)
{
    return ((uint32_t)raw * 3300u + 2047u) / 4095u;
}

int adc_dev_read_now(uint16_t *ntc, uint16_t *pot)
{
    uint16_t pot_raw;
    uint16_t ntc_raw;

    if (pm_adc1_sample(PM_ADC1_PA5_POT, &pot_raw) == 0) {
        return 0;
    }
    if (pm_adc1_sample(PM_ADC1_PA4_NTC, &ntc_raw) == 0) {
        return 0;
    }
    *pot = pot_raw;
    *ntc = ntc_raw;
    return 1;
}

void adc_dev_cmd(cmd_port_t *c)
{
    uint16_t pot = 0u;
    uint16_t ntc = 0u;

    if (pm_adc1_ready() == 0) {
        pm_cmd_out_last_set("ADC err=init");
        pm_cmd_out_tx_str(c, "ADC err=init\r\n");
        return;
    }
    if (pm_adc1_sample(PM_ADC1_PA5_POT, &pot) == 0) {
        pm_cmd_out_last_set("ADC err=tout pa5");
        pm_cmd_out_tx_str(c, "ADC err=timeout ch=pa5\r\n");
        return;
    }
    if (pm_adc1_sample(PM_ADC1_PA4_NTC, &ntc) == 0) {
        pm_cmd_out_last_set("ADC err=tout pa4");
        pm_cmd_out_tx_str(c, "ADC err=timeout ch=pa4\r\n");
        return;
    }
    pm_cmd_out_last_num("ADC ok ", adc_dev_to_mv(pot), "mV");
    pm_cmd_out_tx_str(c, "ADC pot=");
    pm_cmd_out_tx_u32(c, pot);
    pm_cmd_out_tx_str(c, " pot_mv=");
    pm_cmd_out_tx_u32(c, adc_dev_to_mv(pot));
    pm_cmd_out_tx_str(c, " ntc=");
    pm_cmd_out_tx_u32(c, ntc);
    pm_cmd_out_tx_str(c, " ntc_mv=");
    pm_cmd_out_tx_u32(c, adc_dev_to_mv(ntc));
    pm_cmd_out_tx_str(c, " vref=3300\r\n");
}
