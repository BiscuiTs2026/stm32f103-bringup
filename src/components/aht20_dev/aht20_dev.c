/*
 * aht20_dev.c - AHT20 job moved out of src/app/main.c (split step S5).
 *
 * The bus is reached through pm_i2c1_ops(); the screen state arrives through
 * the hooks because a multi-page OLED push must not be cut in by a trigger
 * that re-initialises I2C1.
 */
#include "aht20_dev.h"

#include "pm_cmd.h"
#include "pm_i2c1.h"
#include "pm_osal.h"

static aht20_hooks_t s_hooks;

/* ------------------------------------------------ AHT20 temperature/humidity
 * Standard flow from the AHT20 datasheet: trigger 0xAC/0x33/0x00, wait ~80 ms,
 * then read 6 bytes. The read is issued as a repeated START while the bus is
 * still owned by this master, which is the path that mock tests never touched.
 * Conversion (datasheet 6.2):
 *   RH% = raw_h / 2^20 * 100
 *   T C = raw_t / 2^20 * 200 - 50
 * Reported as tenths so no float support is needed. */

/* The 80 ms conversion window runs from the main loop, not inside the command
 * handler: a command must never block the heartbeat or the other port, and it
 * must always produce exactly one reply. */
#define AHT_WAIT_MS 80u
#define AHT_TRIES   3u

typedef struct {
    cmd_port_t *c;          /* where the reply goes */
    uint32_t    t0;
    uint8_t     active;
    uint8_t     repeated;
    uint8_t     attempt;
} aht_job_t;

static aht_job_t s_aht;

/* Background sample for the live page: triggered every AHT_BG_PERIOD_MS with
 * no reply port. Only a successful read replaces the cache, so the page shows
 * the last good value (or "--" before the first one) and never 0. */
#define AHT_BG_PERIOD_MS 2000u

static int32_t  s_aht_rh10;
static int32_t  s_aht_t10;
static uint8_t  s_aht_have;
static uint32_t s_aht_next = AHT_BG_PERIOD_MS;

static void aht_reply_raw(cmd_port_t *c, const uint8_t *d)
{
    size_t i;

    pm_cmd_out_tx_str(c, " raw=");
    for (i = 0u; i < 6u; i++) {
        if (i != 0u) {
            pm_cmd_out_tx_str(c, " ");
        }
        pm_cmd_out_tx_hex_byte(c, d[i]);
    }
}

static void aht_reply_err(cmd_port_t *c, const uint8_t *d, const char *why)
{
    if (c == NULL) {
        return;                        /* background sample: no port, no reply */
    }
    pm_cmd_out_last_two("AHT err=", why);
    pm_cmd_out_tx_str(c, "AHT err=");
    pm_cmd_out_tx_str(c, why);
    aht_reply_raw(c, d);
    pm_cmd_out_tx_str(c, "\r\n");
}

static void aht_reply_ok(cmd_port_t *c, const uint8_t *d, int repeated)
{
    uint32_t raw_h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | ((uint32_t)d[3] >> 4);
    uint32_t raw_t = (((uint32_t)d[3] & 0x0Fu) << 16) | ((uint32_t)d[4] << 8) | (uint32_t)d[5];
    uint32_t rh10  = (raw_h * 1000u) / 1048576u;
    int32_t  t10   = (int32_t)((raw_t * 2000u) / 1048576u) - 500;

    if (c == NULL) {
        s_aht_rh10 = (int32_t)rh10;
        s_aht_t10  = t10;
        s_aht_have = 1u;
        return;
    }
    {
        char  lb[16];
        char *lp = lb;

        lp = pm_cmd_sput_s(lp, "AHT ok ", 7u);
        lp = pm_cmd_sput_f1(lp, (int32_t)rh10);
        *lp = '\0';
        pm_cmd_out_last_set(lb);
    }
    pm_cmd_out_tx_str(c, "AHT ok rh=");
    pm_cmd_out_tx_fixed1(c, (int32_t)rh10);
    pm_cmd_out_tx_str(c, " t=");
    pm_cmd_out_tx_fixed1(c, t10);
    aht_reply_raw(c, d);
    pm_cmd_out_tx_str(c, " rst=");
    pm_cmd_out_tx_u32(c, (uint32_t)repeated);
    pm_cmd_out_tx_str(c, "\r\n");
}

void aht20_start(cmd_port_t *c, int repeated)
{
    static const uint8_t zero[6] = { 0u, 0u, 0u, 0u, 0u, 0u };

    if (s_aht.active != 0u) {
        pm_cmd_out_tx_str(c, "AHT err=busy pending\r\n");
        return;
    }
    /* pm_i2c1_ops()->aht_trigger() re-initialises I2C1 when the peripheral is
     * off; doing that between two pages of an OLED push wedged the panel
     * (err=io run). */
    if ((s_hooks.ui_busy != NULL) && (s_hooks.ui_busy() != 0)) {
        pm_cmd_out_tx_str(c, "AHT err=busy oled\r\n");
        return;
    }
    if (pm_i2c1_ops()->aht_trigger() == 0) {
        aht_reply_err(c, zero, "io");
        return;
    }
    s_aht.c        = c;
    s_aht.repeated = (uint8_t)repeated;
    s_aht.attempt  = 0u;
    s_aht.t0       = pm_osal_now_ms();
    s_aht.active   = 1u;
    /* 80 ms is too short for a 500 ms refresh to catch; paint the NOW row once
     * (one page, ~12 ms) while the sensor waits, well before the read. */
    if (s_hooks.ui_now_row != NULL) {
        s_hooks.ui_now_row();
    }
}

void aht20_service(uint32_t now)
{
    uint8_t    d[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    int        read_ok;
    const char *why;

    if (s_aht.active == 0u) {
        return;
    }
    if ((uint32_t)(now - s_aht.t0) < AHT_WAIT_MS) {
        return;
    }

    read_ok = pm_i2c1_ops()->aht_read(d, 6u, (int)s_aht.repeated);

    if ((read_ok != 0) && ((d[0] & 0x80u) == 0u)) {
        s_aht.active = 0u;
        aht_reply_ok(s_aht.c, d, (int)s_aht.repeated);
        return;
    }
    /* Keep the reason that matches the data actually observed. */
    why = ((read_ok != 0) && ((d[0] & 0x80u) != 0u)) ? "busy" : "io";

    s_aht.attempt++;
    if (s_aht.attempt >= AHT_TRIES) {
        s_aht.active = 0u;
        aht_reply_err(s_aht.c, d, why);
        return;
    }
    if (pm_i2c1_ops()->aht_trigger() == 0) {
        s_aht.active = 0u;
        aht_reply_err(s_aht.c, d, why);
        return;
    }
    s_aht.t0 = now;
}

/* Keeps the live page's RH/T fresh without blocking the 500 ms refresh: one
 * trigger every 2 s, the result is picked up by aht20_service() 80 ms later. */
void aht20_bg_tick(uint32_t now)
{
    if ((int32_t)(now - s_aht_next) < 0) {
        return;
    }
    s_aht_next = now + AHT_BG_PERIOD_MS;
    if (s_aht.active != 0u) {
        return;                        /* a manual AHT/AHTS owns the sensor */
    }
    /* The OLED push is a long multi-page I2C sequence; re-initialising the bus
     * in the middle of it wedged the panel (seen as a run of err=io). */
    if ((s_hooks.ui_busy != NULL) && (s_hooks.ui_busy() != 0)) {
        return;
    }
    if (pm_i2c1_ops()->aht_trigger() == 0) {
        return;
    }
    s_aht.c        = NULL;
    s_aht.repeated = 0u;
    s_aht.attempt  = 0u;
    s_aht.t0       = now;
    s_aht.active   = 1u;
}

void aht20_init(const aht20_hooks_t *h)
{
    if (h == NULL) {
        s_hooks.ui_busy    = NULL;
        s_hooks.ui_now_row = NULL;
        return;
    }
    s_hooks = *h;
}

int aht20_busy(void)
{
    return (s_aht.active != 0u) ? 1 : 0;
}

int aht20_reading(int32_t *rh10, int32_t *t10)
{
    if (s_aht_have == 0u) {
        return 0;
    }
    if (rh10 != NULL) {
        *rh10 = s_aht_rh10;
    }
    if (t10 != NULL) {
        *t10 = s_aht_t10;
    }
    return 1;
}
