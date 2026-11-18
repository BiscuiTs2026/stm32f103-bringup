/*
 * oled_ch1116.h - minimal CH1116 128x64 OLED driver (component layer).
 *
 * Facts this driver is built on:
 *   - HARDWARE.md item 9 / section 7 item 8: driver IC CH1116, I2C address 0x7A,
 *     SCL = PB6, SDA = PB7 (shared bus with the AHT20).
 *   - HARDWARE.md section 8.2: module spec is ZJY130-2864KSWLG01 (1.3 inch,
 *     "2864" = 128x64 dots).
 *   - 0x7A is the 8-bit write address, i.e. 7-bit 0x3D, exactly what the I2C
 *     scan reports because it lists addr << 1.
 *
 * The driver never touches registers. All bus traffic goes through the
 * oled_ch1116_bus_fn callback supplied by the caller, so this file stays
 * portable and the (bring-up) I2C polling code stays in the entry glue.
 *
 * Control byte convention (CH1116/SSD1306 style I2C framing):
 *   Co = 0, D/C# = 0 -> 0x00 then a stream of command bytes
 *   Co = 0, D/C# = 1 -> 0x40 then a stream of GDDRAM data bytes
 */
#ifndef OLED_CH1116_H
#define OLED_CH1116_H

#include <stdint.h>

#define OLED_CH1116_WIDTH       128u
#define OLED_CH1116_PAGES       8u      /* 8 pages x 8 rows = 64 lines */
#define OLED_CH1116_LINE_CHARS  21u     /* 21 * 6 px = 126 px */

#define OLED_CH1116_ADDR8       0x7Au   /* 7-bit 0x3D << 1 */
#define OLED_CH1116_CTRL_CMD    0x00u
#define OLED_CH1116_CTRL_DATA   0x40u

/* The controller has 132 columns of GDDRAM but the 1.3" glass only exposes
 * 128 (ZJY130-2864KSWLG01). Unlike SSD1306 there is no "column 0 = first
 * visible pixel" assumption: the visible window is RAM columns 2..129, so
 * writes must start at column 2. Writing at 0 loses two columns on the left
 * and leaves RAM 128/129 holding power-on garbage at the right edge.
 * Overridable at runtime for the frame A/B test (OLED col <n>). */
#define OLED_CH1116_COL_OFFSET  2u

/* Returns 0 on success, negative on bus failure. */
typedef int (*oled_ch1116_bus_fn)(void *ctx, uint8_t control,
                                  const uint8_t *data, uint16_t len);

typedef enum {
    OLED_CH1116_JOB_NONE = 0,
    OLED_CH1116_JOB_PATTERN,    /* fill every page with a constant / checker */
    OLED_CH1116_JOB_TEXT,       /* push staged ASCII lines */
    OLED_CH1116_JOB_FRAME       /* 1 px border + a line every 16 columns */
} oled_ch1116_job_t;

typedef struct {
    oled_ch1116_bus_fn bus;
    void              *ctx;
    char      line[OLED_CH1116_PAGES][OLED_CH1116_LINE_CHARS + 1u];
    uint8_t   job;
    uint8_t   next_page;
    uint8_t   end_page;
    uint8_t   pattern;          /* 0x00 clear, 0xAA checkerboard, 0xFF all on */
    uint8_t   col_offset;       /* first GDDRAM column written */
    int       err;              /* last bus return code, 0 = no error */
} oled_ch1116_t;

/* Sends the controller init sequence. Blocking for the caller: 24 bytes at
 * 100 kHz, about 2.2 ms. */
int oled_ch1116_init(oled_ch1116_t *o, oled_ch1116_bus_fn bus, void *ctx);

/* 0xAF / 0xAE. One 2-byte I2C frame, about 0.2 ms. */
int oled_ch1116_display(oled_ch1116_t *o, int on);

/* Copies up to 21 characters into the staging buffer for one page. No I2C. */
void oled_ch1116_stage_line(oled_ch1116_t *o, uint8_t page, const char *text);

/* Start an async job. Returns 0 when started, -1 when one is already running
 * or the arguments are out of range. Nothing is transferred until
 * oled_ch1116_service() is called. */
int oled_ch1116_start_pattern(oled_ch1116_t *o, uint8_t pattern);
int oled_ch1116_start_text(oled_ch1116_t *o, uint8_t first_page, uint8_t count);
int oled_ch1116_start_frame(oled_ch1116_t *o);

/* GDDRAM column the next page write starts at. 0..4 leaves 128 bytes inside
 * the 132-column RAM; the module default is OLED_CH1116_COL_OFFSET. */
void    oled_ch1116_set_col_offset(oled_ch1116_t *o, uint8_t col);
uint8_t oled_ch1116_col_offset(const oled_ch1116_t *o);

/* Pushes at most one page (128 data bytes, ~12 ms at 100 kHz) and returns.
 * 1 = more pages remain, 0 = idle or the job just finished (check err). */
int oled_ch1116_service(oled_ch1116_t *o);

/* Drops the running job at a page boundary. Used when a command takes the
 * panel back from the periodic live refresh: the bus is idle between pages,
 * so no transaction is cut in half. Pages already written keep their content;
 * the replacement job rewrites all 8 pages anyway. */
void oled_ch1116_abort(oled_ch1116_t *o);

int oled_ch1116_busy(const oled_ch1116_t *o);
int oled_ch1116_err(const oled_ch1116_t *o);

#endif /* OLED_CH1116_H */
