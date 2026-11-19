/*
 * oled_ui_page.h - OLED page content staging (component layer).
 *
 * Moved out of src/app/main.c in S9a. The panel and the state the entry layer
 * still owns are injected as two parameters; 9b makes both internal to the
 * component and deletes the view construction in main.c.
 */
#ifndef OLED_UI_PAGE_H
#define OLED_UI_PAGE_H

#include <stdint.h>

#include "pm_cmd_out.h"
#include "oled_ch1116.h"

/* Temporary view built by the entry layer in S9a: the now-job tag plus the
 * three live/static values the page needs. S9b removes it from the glue. */
typedef struct {
    const char *job;        /* now_job(): OLED PUSH / AHT READ / DIST MEAS / IDLE */
    uint8_t     live;
    uint32_t    hold_until;
    uint32_t    last_ms;
} oled_ui_view_t;

const char *oled_ui_page_mode_name(oled_ch1116_t *o, const oled_ui_view_t *v, uint32_t now);
void        oled_ui_page_now_row(oled_ch1116_t *o, const oled_ui_view_t *v);
void        oled_ui_page_live(oled_ch1116_t *o, const oled_ui_view_t *v);
void        oled_ui_page_dump(cmd_port_t *c, oled_ch1116_t *o, const oled_ui_view_t *v,
                              const char *screen, int with_lines);

#endif /* OLED_UI_PAGE_H */
