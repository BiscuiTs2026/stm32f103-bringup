/*
 * pm_track_stm32f1.c - PB14 line-tracking port, STM32F1 registers only.
 *
 * The core owns the polarity decision; this file only sets the pin up and
 * reports the raw level. PB14 is shared with SPI2 MISO on this board, so both
 * modules must never be open at the same time (SPI2 demo is off by default).
 */
#include "pm_track.h"

#include "stm32f1xx.h"

static uint8_t s_opened;

static pm_err_t f1_track_open(void *ctx, pm_track_t *t,
                              const pm_track_cfg_t *cfg)
{
    PM_UNUSED(ctx);
    PM_UNUSED(t);

    if (cfg == NULL) {
        return PM_ERR_NULL;
    }
    if (cfg->hw_id != 0u) {
        return PM_ERR_RANGE;
    }
    if (s_opened != 0u) {
        return PM_ERR_BUSY;
    }

    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /* PB14 input with pull-up: CRH bits 27:24 = 1000. */
    GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 24)) | (0x8u << 24);
    GPIOB->BSRR = (1u << 14);

    s_opened = 1u;
    return PM_OK;
}

static void f1_track_close(void *ctx, pm_track_t *t)
{
    PM_UNUSED(ctx);
    PM_UNUSED(t);

    /* Input floating releases the pull-up for the next owner of PB14. */
    GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 24)) | (0x4u << 24);
    s_opened = 0u;
}

static pm_err_t f1_track_read(void *ctx, pm_track_t *t, bool *high)
{
    PM_UNUSED(ctx);
    PM_UNUSED(t);

    if (high == NULL) {
        return PM_ERR_NULL;
    }
    if (s_opened == 0u) {
        return PM_ERR_STATE;
    }
    *high = ((GPIOB->IDR & (1u << 14)) != 0u);
    return PM_OK;
}

const pm_track_ops_t pm_f1_track_ops = {
    .open  = f1_track_open,
    .close = f1_track_close,
    .read  = f1_track_read,
};
