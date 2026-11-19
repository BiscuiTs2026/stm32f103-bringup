/*
 * adc_dev.h - ADC command and the live-page ADC sample.
 *
 * The vendor handle, the channel registers and the calibration stay in the
 * bsp (pm_adc1.h); this component only sees raw 12-bit values. The page draws
 * by calling adc_dev_read_now() at paint time, which is the same moment the
 * old entry-layer cache read ran.
 */
#ifndef ADC_DEV_H
#define ADC_DEV_H

#include <stdint.h>

#include "pm_cmd_out.h"

/* ADC command reply: pot= pot_mv= ntc= ntc_mv= vref=3300. */
void adc_dev_cmd(cmd_port_t *c);   // arch-allow: plain C prototype, not an SPL call

/* raw * 3300 / 4095, integer division (same rounding as the model repo). */
uint32_t adc_dev_to_mv(uint16_t raw);   // arch-allow: plain C prototype, not an SPL call

/* Samples PA5 then PA4 and returns 1 on success. On failure both outputs stay
 * untouched and 0 is returned, so the caller blanks the row instead of showing
 * a stale or zero value. */
int adc_dev_read_now(uint16_t *ntc, uint16_t *pot);   // arch-allow: plain C prototype, not an SPL call

#endif /* ADC_DEV_H */
