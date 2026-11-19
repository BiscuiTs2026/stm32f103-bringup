/*
 * oled_ui_job.c - CH1116 screen job state machine (component layer).
 *
 * S9b move: state, command handling, the 500 ms refresh and the boot sequence
 * come from src/app/main.c unchanged except for their state source. Page
 * content lives in oled_ui_page.c and is built from an internal view.
 *
 * Bus access is bounded polling of the I2C1 status register, not an interrupt
 * state machine: the peripheral has no I2C IRQ wired up and adding one would
 * change the port layer for a single device. One service call pushes a single
 * 128-byte page = 130 bytes on the wire, ~12 ms at 100 kHz. A full screen is
 * split over 8 calls (8 main loop iterations), so the loop is never blocked
 * for a whole-frame transfer.
 */
#include "oled_ui.h"

#include "pm_common.h"
#include "pm_cmd.h"
#include "pm_cmd_out.h"
#include "pm_i2c1.h"
#include "pm_osal.h"
#include "aht20_dev.h"
#include "oled_ch1116.h"
#include "oled_ui_page.h"

#define OLED_LIVE_PERIOD_MS 500u

/* Live is the default after boot. A static screen holds the panel for this long
 * by pushing the two next-due timestamps forward; `OLED off` clears live. */
#define OLED_STATIC_HOLD_MS 3000u

/* Live mode measures on its own at 1 Hz, offset by half an OLED period from the
 * display refresh (see oled_dist_tick()). */
#define DIST_LIVE_PERIOD_MS 1000u

static oled_ch1116_t s_oled;

/* The port a multi-page job replies on; NULL = no job running. */
static cmd_port_t *s_oled_req;

/* The hooks are installed by oled_ui_boot() before the first tick. */
static const oled_ui_hooks_t *s_hooks;

static const char *s_oled_name;
static uint32_t    s_oled_t0;

/* `OLED live on`: refresh the status lines every OLED_LIVE_PERIOD_MS. One job
 * at a time; a period is skipped rather than queued when the panel is busy. */
static uint8_t     s_oled_live;        /* 1 = periodic refresh enabled */
static uint8_t     s_oled_live_job;    /* the running job is a live refresh */
static cmd_port_t *s_oled_live_port;   /* where live refresh replies go */
static uint32_t    s_oled_live_next;   /* ms of the next refresh */
static uint32_t    s_oled_hold_until;  /* non-zero: a static page holds the panel */
static uint32_t    s_oled_last_ms;     /* duration of the last whole-frame push */
static uint8_t     s_oled_silent;      /* one-page NOW repaint: do not reply */

/* The 1 Hz distance measurement is scheduled here because the OLED live mode is
 * what drives it; the actual trigger stays in the entry layer until S6. */
static uint32_t    s_dist_live_next;

static void oled_tx_counters(cmd_port_t *c)
{
    pm_i2c1_stats_t st;

    pm_i2c1_stats(&st);
    pm_cmd_out_tx_str(c, " af=");
    pm_cmd_out_tx_u32(c, st.af);
    pm_cmd_out_tx_str(c, " berr=");
    pm_cmd_out_tx_u32(c, st.berr);
    pm_cmd_out_tx_str(c, " to=");
    pm_cmd_out_tx_u32(c, st.to);
}

static void oled_err_reply(cmd_port_t *c, const char *why)
{
    pm_cmd_out_tx_str(c, "OLED err=");
    pm_cmd_out_tx_str(c, why);
    oled_tx_counters(c);
    pm_cmd_out_tx_str(c, "\r\n");
}

/* The background job that is running right now, for the NOW row. */
static const char *now_job(void)
{
    if (s_oled_req != NULL) {
        return "OLED PUSH";
    }
    if (aht20_busy() != 0) {
        return "AHT READ";
    }
    if ((s_hooks->dist_busy != NULL) && (s_hooks->dist_busy() != 0)) {
        return "DIST MEAS";
    }
    return "IDLE";
}

/* Every screen command paints all 8 pages, so the result never depends on what
 * was on the panel before. `OLED show` used to write only pages 0..2, which
 * left the lower half of a previous `frame` on screen and is exactly what the
 * photo showed. */
typedef enum {
    OLED_SCREEN_NONE = 0,
    OLED_SCREEN_CLEAR,
    OLED_SCREEN_TEST,
    OLED_SCREEN_FRAME,
    OLED_SCREEN_SHOW,
    OLED_SCREEN_LIVE
} oled_screen_t;

static oled_screen_t s_oled_last;

static const char *oled_screen_name(oled_screen_t s)
{
    switch (s) {
    case OLED_SCREEN_CLEAR: return "clear";
    case OLED_SCREEN_TEST:  return "test";
    case OLED_SCREEN_FRAME: return "frame";
    case OLED_SCREEN_SHOW:  return "show";
    case OLED_SCREEN_LIVE:  return "live";
    default:                return "none";
    }
}

/* The live/static values the page functions read. */
static void oled_view(oled_ui_view_t *v)
{
    v->job        = now_job();
    v->live       = s_oled_live;
    v->hold_until = s_oled_hold_until;
    v->last_ms    = s_oled_last_ms;
}

/* A DIST echo window (~20 ms) or an AHT conversion (80 ms) is far shorter than
 * the 500 ms page period, so a normal frame would almost never show it. Repaint
 * row 1 alone at job start: one page is one I2C transaction (~12 ms), so it
 * still finishes well before the AHT read. Never called from the refresh tick
 * and never in the middle of another panel job. */
void oled_ui_now_row(void)
{
    oled_ui_view_t v;
    uint32_t now = pm_osal_now_ms();

    if ((s_oled_live == 0u) || (s_oled_req != NULL)) {
        return;
    }
    if (s_oled_last != OLED_SCREEN_LIVE) {
        return;                        /* a static page owns the panel */
    }
    if ((s_oled_hold_until != 0u) && ((int32_t)(s_oled_hold_until - now) > 0)) {
        return;
    }
    oled_view(&v);
    oled_ui_page_now_row(&s_oled, &v);
    if (oled_ch1116_start_text(&s_oled, 1u, 1u) != 0) {
        return;
    }
    s_oled_req      = s_oled_live_port;
    s_oled_name     = "now";
    s_oled_t0       = now;
    s_oled_live_job = 1u;
    s_oled_silent   = 1u;
}

/* Starts a full-screen job; pages 3..7 of a show are written blank. */
static int oled_start_screen(oled_screen_t s)
{
    oled_ui_view_t v;

    switch (s) {
    case OLED_SCREEN_CLEAR:
        return oled_ch1116_start_pattern(&s_oled, 0x00u);
    case OLED_SCREEN_TEST:
        return oled_ch1116_start_pattern(&s_oled, 0xAAu);
    case OLED_SCREEN_FRAME:
        return oled_ch1116_start_frame(&s_oled);
    case OLED_SCREEN_SHOW:
        oled_view(&v);
        oled_ui_page_live(&s_oled, &v);
        return oled_ch1116_start_text(&s_oled, 0u, (uint8_t)OLED_CH1116_PAGES);
    default:
        return -1;
    }
}

/* One live refresh: the same paged job as `show`, always all 8 pages so a row
 * that becomes empty (DIST value -> NONE) leaves no residue. The reply is sent
 * when the job completes, so ms= is the real 8-page transfer time. */
static int oled_start_live_job(cmd_port_t *port, const char *name, uint32_t now)
{
    oled_ui_view_t v;

    oled_view(&v);
    oled_ui_page_live(&s_oled, &v);
    if (oled_ch1116_start_text(&s_oled, 0u, (uint8_t)OLED_CH1116_PAGES) != 0) {
        return -1;
    }
    s_oled_req      = port;
    s_oled_name     = name;
    s_oled_t0       = now;
    s_oled_live_job = 1u;
    s_oled_last     = OLED_SCREEN_LIVE;
    return 0;
}

/* (Re)start the periodic refresh. `first_ms` is when the first frame is due:
 * boot passes now + one period so the boot clear has finished, while
 * `OLED live on` passes now. The 1 Hz distance measurement sits half a period
 * away from the display refresh. */
static void oled_live_arm(cmd_port_t *port, uint32_t first_ms)
{
    s_oled_live      = 1u;   s_oled_live_port = port;   s_oled_last = OLED_SCREEN_LIVE;
    s_oled_live_next = first_ms;
    s_oled_hold_until = 0u;
    s_dist_live_next = first_ms + (OLED_LIVE_PERIOD_MS / 2u);
}

void oled_ui_cmd(cmd_port_t *c, const char *arg)
{
    oled_screen_t s = OLED_SCREEN_NONE;
    const char   *rest = NULL;

    /* A pattern command, `show`, `on`/`off` or `live off` takes the panel back
     * even in the middle of a live refresh. Command handling and oled_service()
     * both run in the main loop, so this always lands on a page boundary and no
     * I2C frame is cut in half. */
    if (s_oled_live_job != 0u) {
        int takeover = pm_cmd_ci_equal(arg, "FRAME") || pm_cmd_ci_equal(arg, "TEST") ||
                       pm_cmd_ci_equal(arg, "CLEAR") || pm_cmd_ci_equal(arg, "SHOW") ||
                       pm_cmd_ci_equal(arg, "ON") || pm_cmd_ci_equal(arg, "OFF") ||
                       pm_cmd_ci_starts(arg, "LIVE ", &rest) ||
                       pm_cmd_ci_starts(arg, "COL", &rest);

        if (takeover != 0) {
            oled_ch1116_abort(&s_oled);
            s_oled_req      = NULL;
            s_oled_live_job = 0u;
            s_oled_silent   = 0u;
        }
    }

    /* Text dump of the page: no I2C, so it is allowed even mid-push and is the
     * only way to read MODE/STATIC/OFF back while the panel is blank or shows
     * a static pattern. */
    if (pm_cmd_ci_equal(arg, "DUMP")) {
        oled_ui_view_t v;

        oled_view(&v);
        oled_ui_page_dump(c, &s_oled, &v, oled_screen_name(s_oled_last),
                          ((s_oled_last == OLED_SCREEN_LIVE) ||
                           (s_oled_last == OLED_SCREEN_SHOW)) ? 1 : 0);
        pm_cmd_out_last_set("OLED dump");
        return;
    }

    if (s_oled_req != NULL) {
        pm_cmd_out_tx_str(c, "OLED err=busy pending\r\n");
        return;
    }
    /* An AHT/AHTS read is pending (80 ms window); do not cut in. */
    if (aht20_busy() != 0) {
        pm_cmd_out_tx_str(c, "OLED err=busy i2c\r\n");
        return;
    }

    if (pm_cmd_ci_equal(arg, "ON")) {
        if (oled_ch1116_display(&s_oled, 1) != 0) {
            oled_err_reply(c, "io");
            return;
        }
        oled_live_arm(c, pm_osal_now_ms());
        pm_cmd_out_last_set("OLED on ok");
        pm_cmd_out_tx_str(c, "OLED on ok display=on live=on");
        oled_tx_counters(c);
        pm_cmd_out_tx_str(c, "\r\n");
        return;
    }
    if (pm_cmd_ci_equal(arg, "OFF")) {
        if (oled_ch1116_display(&s_oled, 0) != 0) {
            oled_err_reply(c, "io");
            return;
        }
        s_oled_live = 0u;   /* stop refreshing and stop the 1 Hz ranging */
        s_oled_hold_until = 0u;
        pm_cmd_out_last_set("OLED off ok");
        pm_cmd_out_tx_str(c, "OLED off ok display=off live=paused");
        oled_tx_counters(c);
        pm_cmd_out_tx_str(c, "\r\n");
        return;
    }

    /* `OLED live on|off`; a bare `OLED live` is a usage error. */
    if (pm_cmd_ci_equal(arg, "LIVE")) {
        pm_cmd_out_tx_str(c, "OLED err=usage\r\n");
        return;
    }
    if (pm_cmd_ci_starts(arg, "LIVE ", &rest)) {
        while (*rest == ' ') {
            rest++;
        }
        if (pm_cmd_ci_equal(rest, "ON")) {
            oled_live_arm(c, pm_osal_now_ms());
            pm_cmd_out_tx_str(c, "OLED live=on period=");
            pm_cmd_out_tx_u32(c, OLED_LIVE_PERIOD_MS);
            pm_cmd_out_tx_str(c, "ms\r\n");
            return;
        }
        if (pm_cmd_ci_equal(rest, "OFF")) {
            s_oled_live = 0u;
            pm_cmd_out_tx_str(c, "OLED live=off\r\n");
            return;
        }
        pm_cmd_out_tx_str(c, "OLED err=usage\r\n");
        return;
    }

    /* Diagnostic for the 132-column RAM / 128-column glass offset. Repaints the
     * last screen so the A/B needs no extra `frame` command. */
    if (pm_cmd_ci_starts(arg, "COL", &rest)) {
        uint32_t v;

        if ((pm_cmd_parse_u32(rest, &v) == 0) || (v > 4u)) {
            pm_cmd_out_tx_str(c, "OLED err=usage col=0..4\r\n");
            return;
        }
        oled_ch1116_set_col_offset(&s_oled, (uint8_t)v);
        pm_cmd_out_tx_str(c, "OLED col=");
        pm_cmd_out_tx_u32(c, v);
        pm_cmd_out_tx_str(c, " ok");
        if (s_oled_last == OLED_SCREEN_LIVE) {
            uint32_t now = pm_osal_now_ms();

            if (oled_start_live_job(c, "redraw", now) == 0) {
                s_oled_live_next = now + OLED_LIVE_PERIOD_MS;
                pm_cmd_out_tx_str(c, " redraw=live");
            }
        } else if (s_oled_last != OLED_SCREEN_NONE) {
            if (oled_start_screen(s_oled_last) == 0) {
                s_oled_req  = c;
                s_oled_name = "redraw";
                s_oled_t0   = pm_osal_now_ms();
                pm_cmd_out_tx_str(c, " redraw=");
                pm_cmd_out_tx_str(c, oled_screen_name(s_oled_last));
            }
        }
        pm_cmd_out_tx_str(c, "\r\n");
        return;
    }

    if (pm_cmd_ci_equal(arg, "CLEAR")) {
        s = OLED_SCREEN_CLEAR;
    } else if (pm_cmd_ci_equal(arg, "TEST")) {
        s = OLED_SCREEN_TEST;
    } else if (pm_cmd_ci_equal(arg, "FRAME")) {
        s = OLED_SCREEN_FRAME;
    } else if (pm_cmd_ci_equal(arg, "SHOW")) {
        s = OLED_SCREEN_SHOW;
    } else {
        pm_cmd_out_tx_str(c, "OLED err=usage\r\n");
        return;
    }

    /* Static screens take the panel for OLED_STATIC_HOLD_MS and then hand it
     * back to live; it is only a temporary takeover, live stays armed. */
    if (oled_start_screen(s) != 0) {
        pm_cmd_out_tx_str(c, "OLED err=busy pending\r\n");
        return;
    }

    s_oled_last  = s;
    s_oled_req   = c;
    s_oled_name  = oled_screen_name(s);
    s_oled_t0    = pm_osal_now_ms();
    pm_cmd_out_last_two("OLED ", oled_screen_name(s));
}

/* Called once per main loop iteration: at most one page per call. */
void oled_ui_service(void)
{
    cmd_port_t *c;
    int         was_live;
    uint32_t    ms;

    if (s_oled_req == NULL) {
        return;
    }
    if (oled_ch1116_service(&s_oled) != 0) {
        return;
    }

    c           = s_oled_req;
    was_live    = (int)s_oled_live_job;
    s_oled_req  = NULL;
    s_oled_live_job = 0u;
    ms          = (uint32_t)(pm_osal_now_ms() - s_oled_t0);

    if (s_oled_silent != 0u) {
        s_oled_silent = 0u;
        if (oled_ch1116_err(&s_oled) != 0) {
            oled_ch1116_abort(&s_oled);
        }
        return;                        /* internal NOW repaint, not a reply */
    }
    if (oled_ch1116_err(&s_oled) != 0) {
        oled_err_reply(c, "io");
        return;
    }
    if (was_live != 0) {
        s_oled_last_ms = ms;           /* health value shown as "LIVE ms=" */
    }
    pm_cmd_out_tx_str(c, "OLED ");
    pm_cmd_out_tx_str(c, s_oled_name);
    pm_cmd_out_tx_str(c, " ok ms=");
    pm_cmd_out_tx_u32(c, ms);
    pm_cmd_out_tx_str(c, " col=");
    pm_cmd_out_tx_u32(c, (uint32_t)oled_ch1116_col_offset(&s_oled));
    /* A periodic refresh already implies live=on; pattern commands report that
     * they took the panel back. */
    if (was_live == 0) {
        pm_cmd_out_tx_str(c, " live=");
        pm_cmd_out_tx_str(c, (s_oled_live != 0u) ? "on" : "off");
        /* A static screen only holds the panel briefly before live returns. */
        if (s_oled_live != 0u) {
            s_oled_live_next = pm_osal_now_ms() + OLED_STATIC_HOLD_MS;
            s_oled_hold_until = s_oled_live_next;
            s_dist_live_next = s_oled_live_next + (OLED_LIVE_PERIOD_MS / 2u);
            pm_cmd_out_tx_str(c, " hold="); pm_cmd_out_tx_u32(c, OLED_STATIC_HOLD_MS); pm_cmd_out_tx_str(c, "ms");
        }
    }
    oled_tx_counters(c);
    pm_cmd_out_tx_str(c, "\r\n");
}

/* Called once per main loop iteration. One refresh per 500 ms; when the panel
 * is still busy the period is skipped, never queued. */
static void oled_live_tick(uint32_t now)
{
    if (s_oled_live == 0u) {
        return;
    }
    if (s_oled_req != NULL) {
        return;
    }
    if ((int32_t)(now - s_oled_live_next) < 0) {
        return;
    }
    if (aht20_busy() != 0) {
        return;                        /* do not interleave with the AHT window */
    }
    s_oled_live_next = now + OLED_LIVE_PERIOD_MS;
    s_oled_hold_until = 0u;            /* the static page's hold has expired */
    (void)oled_start_live_job(s_oled_live_port, "live", now);
}

/* Live mode triggers the HC-SR04 itself: one non-blocking dist_start() per
 * second, reusing the manual path (no second copy of the sequence). It is
 * offset half an OLED period from the display refresh, so the 10 us pulse and
 * the <=23 ms echo window sit in the gap between two page transfers. The result
 * goes into the DIST last-value slots and the next refresh (<=500 ms later)
 * stages it; a measurement that is still in flight just skips this period. */
static void oled_dist_tick(uint32_t now)
{
    if (s_oled_live == 0u) {
        return;
    }
    if ((int32_t)(now - s_dist_live_next) < 0) {
        return;
    }
    s_dist_live_next = now + DIST_LIVE_PERIOD_MS;
    s_hooks->dist_live_poke(s_oled_live_port);
}

void oled_ui_live_tick(uint32_t now)
{
    oled_dist_tick(now);
    oled_live_tick(now);
}

int oled_ui_busy(void)
{
    return (s_oled_req != NULL) ? 1 : 0;
}

/* Returns the argument after "OLED" ("" for a bare OLED), or NULL if the line
 * is a different command. */
const char *oled_ui_arg(const char *line)
{
    const char *p = line;
    size_t      i;

    for (i = 0u; i < 4u; i++) {
        char a = p[i];

        if ((a >= 'a') && (a <= 'z')) {
            a = (char)(a - 'a' + 'A');
        }
        if (a != "OLED"[i]) {
            return NULL;
        }
    }
    p = &line[4];
    if (*p == '\0') {
        return p;
    }
    if (*p != ' ') {
        return NULL;
    }
    while (*p == ' ') {
        p++;
    }
    return p;
}

/* One-shot bring-up: init sequence, then a synchronous clear so the panel
 * starts from a known state. 8 pages, ~100 ms, once at boot. */
void oled_ui_boot(const oled_ui_hooks_t *h, cmd_port_t *live_port)
{
    uint8_t guard = 0u;

    s_hooks = h;
    if (oled_ch1116_init(&s_oled, pm_i2c1_ops()->write, NULL) != 0) {
        pm_cmd_out_uart_write_str("OLED CH1116 addr=7A init FAILED\r\n");
    } else {
        pm_cmd_out_uart_write_str("OLED CH1116 addr=7A init ok, col=");
        pm_cmd_out_uart_write_u32((uint32_t)oled_ch1116_col_offset(&s_oled));
        pm_cmd_out_uart_write_str(", clearing\r\n");
        (void)oled_ch1116_start_pattern(&s_oled, 0x00u);
        while ((oled_ch1116_service(&s_oled) != 0) &&
               (guard < (uint8_t)OLED_CH1116_PAGES)) {
            guard++;
        }
        (void)oled_ch1116_display(&s_oled, 1);
        if (oled_ch1116_err(&s_oled) != 0) {
            pm_cmd_out_uart_write_str("OLED clear FAILED\r\n");
        } else {
            /* The boot clear is a real screen state: `OLED col n` repaints it
             * so an A/B before the first pattern command still shows a redraw. */
            s_oled_last = OLED_SCREEN_CLEAR;
            pm_cmd_out_uart_write_str("OLED ready (128x64, 6x8 font)\r\n");
        }
    }

    /* Boot default: live. Armed only after the init sequence and the
     * synchronous clear, so the first refresh cannot race the clear. One
     * period of delay leaves the boot messages on screen briefly. */
    oled_live_arm(live_port, pm_osal_now_ms() + OLED_LIVE_PERIOD_MS);
}
