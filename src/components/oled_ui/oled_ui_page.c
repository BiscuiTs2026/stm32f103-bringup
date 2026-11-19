/*
 * oled_ui_page.c - OLED page content staging (component layer).
 *
 * S9a move: the four page functions come from src/app/main.c unchanged except
 * for their state source. They read the panel through `o` and the live/static
 * values through `v`; no other logic was touched. S9b removes `v` again by
 * moving the job state into this component.
 */
#include "oled_ui_page.h"

#include "pm_common.h"
#include "pm_cmd.h"
#include "pm_cmd_out.h"
#include "pm_i2c1.h"
#include "pm_osal.h"
#include "adc_dev.h"
#include "aht20_dev.h"
#include "keypad_dev.h"

const char *oled_ui_page_mode_name(oled_ch1116_t *o, const oled_ui_view_t *v, uint32_t now)
{
    PM_UNUSED(o);

    if (v->live == 0u) {
        return "off";
    }
    if ((v->hold_until != 0u) &&
        ((int32_t)(v->hold_until - now) > 0)) {
        return "static";
    }
    return "live";
}

/* The live page, 8 rows x 21 columns. Also used by `OLED show`, so a static
 * text page is the same content the panel shows on its own. */

/* Row 1 only; shared with the short-job repaint below. */
void oled_ui_page_now_row(oled_ch1116_t *o, const oled_ui_view_t *v)
{
    char  b[OLED_CH1116_LINE_CHARS + 1u];
    char *p = pm_cmd_sput_s(b, "NOW : ", 6u);

    p = pm_cmd_sput_s(p, v->job, 15u);
    *p = '\0';
    oled_ch1116_stage_line(o, 1u, b);
}

void oled_ui_page_live(oled_ch1116_t *o, const oled_ui_view_t *v)
{
    char     b[OLED_CH1116_LINE_CHARS + 1u];
    char    *p;
    uint32_t now = pm_osal_now_ms();
    pm_i2c1_stats_t st;
    int32_t  rh10 = 0;
    int32_t  t10  = 0;
    int      have = aht20_reading(&rh10, &t10);
    uint16_t ntc  = 0u;
    uint16_t pot  = 0u;
    int      adc_ok;
    uint32_t k1   = 0u;
    uint32_t k2   = 0u;

    /* Row 0: MODE (8) + UP (8). */
    p = pm_cmd_sput_s(b, "MODE=", 5u);
    if (v->live == 0u) {
        p = pm_cmd_sput_s(p, "OFF", 8u);
    } else if ((v->hold_until != 0u) &&
               ((int32_t)(v->hold_until - now) > 0)) {
        char     m[9];
        char    *q = m;

        q = pm_cmd_sput_s(q, "STAT", 4u);
        q = pm_cmd_sput_pad(q, ((v->hold_until - now) + 999u) / 1000u, 2u);
        *q++ = 'S';
        *q = '\0';
        p = pm_cmd_sput_s(p, m, 8u);
    } else {
        p = pm_cmd_sput_s(p, "LIVE", 8u);
    }
    p = pm_cmd_sput_s(p, "UP=", 3u);
    p = pm_cmd_sput_pad(p, now / 1000u, 4u);
    *p++ = 'S';
    *p = '\0';
    oled_ch1116_stage_line(o, 0u, b);

    oled_ui_page_now_row(o, v);

    p = pm_cmd_sput_s(b, "LAST: ", 6u);
    p = pm_cmd_sput_s(p, pm_cmd_out_last(), 15u);
    *p = '\0';
    oled_ch1116_stage_line(o, 2u, b);

    p = pm_cmd_sput_s(b, "RH=", 3u);
    p = (have != 0) ? pm_cmd_sput_f1(p, rh10) : pm_cmd_sput_s(p, "--.-", 4u);
    p = pm_cmd_sput_s(p, " T=", 3u);
    p = (have != 0) ? pm_cmd_sput_f1(p, t10) : pm_cmd_sput_s(p, "--.-", 4u);
    *p++ = 'C';
    *p = '\0';
    oled_ch1116_stage_line(o, 3u, b);

    adc_ok = adc_dev_read_now(&ntc, &pot);   // arch-allow: component accessor, not an SPL call
    p = pm_cmd_sput_s(b, "NTC=", 4u);
    if (adc_ok != 0) {
        p = pm_cmd_sput_pad(p, ntc, 4u);
    } else {
        p = pm_cmd_sput_s(p, "----", 4u);
    }
    p = pm_cmd_sput_s(p, " POT=", 5u);
    if (adc_ok != 0) {
        p = pm_cmd_sput_pad(p, pot, 4u);
    } else {
        p = pm_cmd_sput_s(p, "----", 4u);
    }
    *p = '\0';
    oled_ch1116_stage_line(o, 4u, b);

    keypad_dev_counts(&k1, &k2);
    p = pm_cmd_sput_s(b, "K1=", 3u);
    p = pm_cmd_sput_pad(p, k1, 5u);
    p = pm_cmd_sput_s(p, " K2=", 4u);
    p = pm_cmd_sput_pad(p, k2, 5u);
    *p = '\0';
    oled_ch1116_stage_line(o, 5u, b);

    pm_i2c1_stats(&st);
    p = pm_cmd_sput_s(b, "I2C af=", 7u);
    p = pm_cmd_sput_pad(p, st.af, 2u);
    p = pm_cmd_sput_s(p, " berr=", 6u);
    p = pm_cmd_sput_pad(p, st.berr, 2u);
    *p = '\0';
    oled_ch1116_stage_line(o, 6u, b);

    p = pm_cmd_sput_s(b, "LIVE ms=", 8u);
    p = (v->last_ms != 0u) ? pm_cmd_sput_pad(p, v->last_ms, 3u)
                           : pm_cmd_sput_s(p, "---", 3u);
    *p = '\0';
    oled_ch1116_stage_line(o, 7u, b);
}

void oled_ui_page_dump(cmd_port_t *c, oled_ch1116_t *o, const oled_ui_view_t *v,
                       const char *screen, int with_lines)
{
    uint32_t now = pm_osal_now_ms();
    uint8_t  pg;

    pm_cmd_out_tx_str(c, "OLED dump mode=");
    pm_cmd_out_tx_str(c, oled_ui_page_mode_name(o, v, now));
    if ((v->live != 0u) && (v->hold_until != 0u) &&
        ((int32_t)(v->hold_until - now) > 0)) {
        pm_cmd_out_tx_str(c, " left=");
        pm_cmd_out_tx_u32(c, ((v->hold_until - now) + 999u) / 1000u);
        pm_cmd_out_tx_str(c, "s");
    }
    pm_cmd_out_tx_str(c, " screen=");
    pm_cmd_out_tx_str(c, screen);
    pm_cmd_out_tx_str(c, "\r\n");
    if (with_lines != 0) {
        for (pg = 0u; pg < (uint8_t)OLED_CH1116_PAGES; pg++) {
            pm_cmd_out_tx_str(c, "OLED L");
            pm_cmd_out_tx_u32(c, pg);
            pm_cmd_out_tx_str(c, "=");
            pm_cmd_out_tx_str(c, o->line[pg]);
            pm_cmd_out_tx_str(c, "\r\n");
        }
    }
}
