/*
 * sonar_dev.c - HC-SR04 measurement job (DIST command).
 *
 * Moved out of src/app/main.c unchanged in behaviour (split step S6b).
 * The pin, EXTI10 and TIM2 time-stamp work stays in
 * src/port/stm32f1/pm_sonar_stm32f1.c; this file owns the non-blocking job,
 * the reply formatting and the last-good value.
 *
 * 2026-11-19: DIST is disabled. TIM2 is handed to the motor PWM, so the
 * timestamps have no time base; sonar_dev_start() replies "DIST err=disabled"
 * and the counter is no longer started. Pins and edge logic are kept as-is so
 * a future time source can re-enable it.
 */
#include "sonar_dev.h"

#include "pm_sonar.h"
#include "pm_osal.h"
#include "pm_cmd.h"
#include "oled_ui.h"

#define DIST_TIMEOUT_MS  60u
#define DIST_MIN_US      100u      /* < 2 cm: noise */
#define DIST_MAX_US      25000u    /* > 4.3 m: the module tops out at 4 m */
/* 0 = DIST disabled: its 1 us time base (TIM2) was reassigned to the motor
 * PWM. Set back to 1 only after a different time source is chosen. */
#define DIST_TIME_BASE_READY 0

typedef struct {
    cmd_port_t       *c;
    uint32_t          t0_ms;
    uint8_t           active;
    uint8_t           auto_live;   /* this measurement was started by live mode */
} dist_job_t;

static dist_job_t s_dist;

#if PM_ENABLE_SONAR
/* Last good distance, kept for OLED show. Cleared on a timeout so the screen
 * never claims a stale reading. */
static uint32_t s_dist_last_cm10;
static uint8_t  s_dist_last_ok;

/* One-line summary for the live page's LAST row: "DIST ok 99.0". */
static void dist_last_ok(uint32_t cm10)
{
    char        b[16];
    char       *p = b;
    const char *pre = "DIST ok ";

    while (*pre != '\0') {
        *p++ = *pre++;
    }
    p = pm_cmd_sput_u32(p, cm10 / 10u);
    *p++ = '.';
    *p++ = (char)('0' + (cm10 % 10u));
    *p = '\0';
    pm_cmd_out_last_set(b);
}
#endif

void sonar_dev_start(cmd_port_t *c, uint8_t auto_live)
{
    /* DIST is disabled: TIM2, the 1 us timestamp source, now belongs to the
     * motor PWM. Manual DIST explains the reason; live mode skips silently so
     * the screen is not flooded once per second. */
    if (DIST_TIME_BASE_READY == 0) {
        if (auto_live == 0u) {
            pm_cmd_out_tx_str(c, "DIST err=disabled\r\n");
        }
        return;
    }

#if PM_ENABLE_SONAR
    if (s_dist.active != 0u) {
        if (auto_live != 0u) {
            return;                        /* live skips this period */
        }
        /* Manual DIST must not run a second HC-SR04 sequence next to the
         * measurement live started. */
        pm_cmd_out_tx_str(c, (s_dist.auto_live != 0u) ? "DIST err=busy live\r\n"
                                           : "DIST err=busy\r\n");
        return;
    }

    s_dist.c         = c;
    s_dist.auto_live = auto_live;
    pm_sonar_clear_edge();
    pm_sonar_pulse();

    s_dist.t0_ms  = pm_osal_now_ms();
    s_dist.active = 1u;
    if (auto_live == 0u) {
        oled_ui_now_row();      /* the echo window is ~20 ms, show it */
    }
#endif /* PM_ENABLE_SONAR */
}

void sonar_dev_service(uint32_t now)
{
#if PM_ENABLE_SONAR
    uint32_t us;
    uint32_t cm10;
    pm_sonar_snapshot_t snap;

    if (s_dist.active == 0u) {
        return;
    }

    pm_sonar_snapshot(&snap);
    if (snap.done == 0u) {
        if ((uint32_t)(now - s_dist.t0_ms) < DIST_TIMEOUT_MS) {
            return;
        }
        s_dist.active    = 0u;
        pm_sonar_clear_edge();
        s_dist_last_ok   = 0u;
        pm_cmd_out_last_set("DIST err=tout");
        if (s_dist.auto_live != 0u) {
            pm_cmd_out_tx_str(s_dist.c, "DIST live ");
            s_dist.auto_live = 0u;
        } else {
            pm_cmd_out_tx_str(s_dist.c, "DIST ");
        }
        pm_cmd_out_tx_str(s_dist.c, "err=timeout\r\n");
        return;
    }

    us = (uint32_t)snap.width_us;
    s_dist.active = 0u;

    if ((us < DIST_MIN_US) || (us > DIST_MAX_US)) {
        /* HC-SR04 holds ECHO high ~38 ms when nothing reflects the burst. */
        s_dist_last_ok = 0u;
        pm_cmd_out_last_set("DIST err=tout");
        if (s_dist.auto_live != 0u) {
            pm_cmd_out_tx_str(s_dist.c, "DIST live ");
            s_dist.auto_live = 0u;
        } else {
            pm_cmd_out_tx_str(s_dist.c, "DIST ");
        }
        pm_cmd_out_tx_str(s_dist.c, "err=timeout\r\n");
        return;
    }

    cm10 = (us * 10u + 29u) / 58u;         /* us / 58 => cm, kept x10 */

    s_dist_last_cm10 = cm10;
    s_dist_last_ok   = 1u;
    if (s_dist.auto_live == 0u) {
        dist_last_ok(cm10);
    }

    if (s_dist.auto_live != 0u) {
        pm_cmd_out_tx_str(s_dist.c, "DIST live ");
        s_dist.auto_live = 0u;
    } else {
        pm_cmd_out_tx_str(s_dist.c, "DIST ");
    }
    pm_cmd_out_tx_str(s_dist.c, "cm=");
    pm_cmd_out_tx_fixed1(s_dist.c, (int32_t)cm10);
    pm_cmd_out_tx_str(s_dist.c, " us=");
    pm_cmd_out_tx_u32(s_dist.c, us);
    pm_cmd_out_tx_str(s_dist.c, "\r\n");
#else
    /* Port not built (PM_ENABLE_SONAR=OFF): s_dist.active is never set, so
     * there is no job to service. DIST still answers err=disabled above. */
    (void)now;
#endif
}

int sonar_dev_busy(void)
{
    return (s_dist.active != 0u) ? 1 : 0;
}

void sonar_dev_live_poke(cmd_port_t *port)
{
    sonar_dev_start(port, 1u);
}
