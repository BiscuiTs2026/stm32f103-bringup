/*
 * pm_adc1.h - ADC1 polling single-shot contract (PA4 NTC, PA5 potentiometer).
 *
 * The implementation lives in src/port/stm32f1/pm_adc1_stm32f1.c; that bsp
 * file is the only place allowed to touch ADC1/GPIOA registers and the vendor
 * HAL types. Callers get a raw 12-bit value and nothing else.
 */
#ifndef PM_ADC1_H
#define PM_ADC1_H

#include <stdint.h>

/* Channel numbering matches ADC1_IN4 / ADC1_IN5 so the header stays readable
 * without vendor macros. */
typedef enum { PM_ADC1_PA4_NTC = 4, PM_ADC1_PA5_POT = 5 } pm_adc1_ch_t;

/* 0 = ok, 1 = HAL_ADC_Init failed, 2 = calibration failed. */
int pm_adc1_init(void);

/* One software-triggered conversion; 1 = ok, 0 = any HAL failure. */
int pm_adc1_sample(pm_adc1_ch_t ch, uint16_t *raw);

/* Non-zero after a successful pm_adc1_init(); the callers keep their
 * "not initialised" reply for that case. */
int pm_adc1_ready(void);

#endif /* PM_ADC1_H */
