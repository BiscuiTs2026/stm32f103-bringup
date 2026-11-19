/*
 * pm_adc1_stm32f1.c - ADC1 polling single-shot for PA4 (NTC) and PA5 (pot).
 *
 * Moved out of src/app/main.c unchanged in behaviour (split step S7a).
 * HARDWARE.md §3: PA4 = NTC (ADC1_IN4, beta 3950, 10 k divider),
 * PA5 = potentiometer (ADC1_IN5, 3.3 V/GND divider). Polling single-shot on
 * purpose; there is no async ADC model in this repository yet.
 * ADCCLK = PCLK2 / 6 = 12 MHz (limit 14 MHz).
 */
#include "pm_adc1.h"

#include "stm32f1xx.h"

static ADC_HandleTypeDef s_adc1;
static uint8_t           s_adc_ok;

int pm_adc1_init(void)
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
        return 1;
    }
    if (HAL_ADCEx_Calibration_Start(&s_adc1) != HAL_OK) {
        return 2;
    }
    s_adc_ok = 1u;
    return 0;
}

int pm_adc1_sample(pm_adc1_ch_t ch, uint16_t *raw)
{
    ADC_ChannelConfTypeDef cfg = { 0 };

    cfg.Channel      = (uint32_t)ch;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    if (HAL_ADC_ConfigChannel(&s_adc1, &cfg) != HAL_OK) {
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

int pm_adc1_ready(void)
{
    return (s_adc_ok != 0u) ? 1 : 0;
}
