/*
 * oled_ch1116.c - CH1116 128x64 OLED, page addressing mode, 5x7 font in a
 * 6x8 cell. No framebuffer, no dynamic allocation: each page is generated on
 * the fly into a 128-byte local buffer, so the ROM cost is only the font and
 * the RAM cost is only the staging strings.
 */
#include "oled_ch1116.h"

#include <string.h>

/* CH1116 init, SSD1306-compatible opcodes. Page addressing mode (0x20 0x02)
 * keeps the write path simple: set page + column, then stream 128 data bytes. */
static const uint8_t s_init_seq[] = {
    0xAEu,              /* display off */
    0xD5u, 0x80u,       /* clock divide / oscillator frequency */
    0xA8u, 0x3Fu,       /* multiplex ratio 1/64 */
    0xD3u, 0x00u,       /* display offset 0 */
    0x40u,              /* start line 0 */
    0x8Du, 0x14u,       /* charge pump on */
    0x20u, 0x02u,       /* page addressing mode */
    0xA1u,              /* segment remap (column 127 -> SEG0) */
    0xC8u,              /* COM scan direction remapped */
    0xDAu, 0x12u,       /* COM pins: alternative, no left/right remap */
    0x81u, 0xCFu,       /* contrast */
    0xD9u, 0xF1u,       /* pre-charge period */
    0xDBu, 0x40u,       /* VCOMH deselect */
    0xA4u,              /* resume from GDDRAM */
    0xA6u,              /* normal (not inverted) */
    0x2Eu,              /* deactivate scrolling */
    0xAFu               /* display on */
};

/* 5x7 ASCII, 0x20..0x7F. One byte per column, bit 0 = top row, bit 6 = bottom
 * row (bit 7 unused). A 6th blank column is added at draw time. */
static const uint8_t s_font5x7[96][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, /*   */
    { 0x00, 0x00, 0x5F, 0x00, 0x00 }, /* ! */
    { 0x00, 0x07, 0x00, 0x07, 0x00 }, /* " */
    { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, /* # */
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, /* $ */
    { 0x23, 0x13, 0x08, 0x64, 0x62 }, /* % */
    { 0x36, 0x49, 0x55, 0x22, 0x50 }, /* & */
    { 0x00, 0x05, 0x03, 0x00, 0x00 }, /* ' */
    { 0x00, 0x1C, 0x22, 0x41, 0x00 }, /* ( */
    { 0x00, 0x41, 0x22, 0x1C, 0x00 }, /* ) */
    { 0x14, 0x08, 0x3E, 0x08, 0x14 }, /* * */
    { 0x08, 0x08, 0x3E, 0x08, 0x08 }, /* + */
    { 0x00, 0x50, 0x30, 0x00, 0x00 }, /* , */
    { 0x08, 0x08, 0x08, 0x08, 0x08 }, /* - */
    { 0x00, 0x60, 0x60, 0x00, 0x00 }, /* . */
    { 0x20, 0x10, 0x08, 0x04, 0x02 }, /* / */
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, /* 0 */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, /* 1 */
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, /* 2 */
    { 0x21, 0x41, 0x45, 0x4B, 0x31 }, /* 3 */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, /* 4 */
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 5 */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, /* 6 */
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, /* 7 */
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, /* 8 */
    { 0x06, 0x49, 0x49, 0x29, 0x1E }, /* 9 */
    { 0x00, 0x36, 0x36, 0x00, 0x00 }, /* : */
    { 0x00, 0x56, 0x36, 0x00, 0x00 }, /* ; */
    { 0x00, 0x08, 0x14, 0x22, 0x41 }, /* < */
    { 0x14, 0x14, 0x14, 0x14, 0x14 }, /* = */
    { 0x41, 0x22, 0x14, 0x08, 0x00 }, /* > */
    { 0x02, 0x01, 0x51, 0x09, 0x06 }, /* ? */
    { 0x32, 0x49, 0x79, 0x41, 0x3E }, /* @ */
    { 0x7E, 0x11, 0x11, 0x11, 0x7E }, /* A */
    { 0x7F, 0x49, 0x49, 0x49, 0x36 }, /* B */
    { 0x3E, 0x41, 0x41, 0x41, 0x22 }, /* C */
    { 0x7F, 0x41, 0x41, 0x22, 0x1C }, /* D */
    { 0x7F, 0x49, 0x49, 0x49, 0x41 }, /* E */
    { 0x7F, 0x09, 0x09, 0x01, 0x01 }, /* F */
    { 0x3E, 0x41, 0x41, 0x51, 0x32 }, /* G */
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, /* H */
    { 0x00, 0x41, 0x7F, 0x41, 0x00 }, /* I */
    { 0x20, 0x40, 0x41, 0x3F, 0x01 }, /* J */
    { 0x7F, 0x08, 0x14, 0x22, 0x41 }, /* K */
    { 0x7F, 0x40, 0x40, 0x40, 0x40 }, /* L */
    { 0x7F, 0x02, 0x04, 0x02, 0x7F }, /* M */
    { 0x7F, 0x04, 0x08, 0x10, 0x7F }, /* N */
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, /* O */
    { 0x7F, 0x09, 0x09, 0x09, 0x06 }, /* P */
    { 0x3E, 0x41, 0x51, 0x21, 0x5E }, /* Q */
    { 0x7F, 0x09, 0x19, 0x29, 0x46 }, /* R */
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, /* S */
    { 0x01, 0x01, 0x7F, 0x01, 0x01 }, /* T */
    { 0x3F, 0x40, 0x40, 0x40, 0x3F }, /* U */
    { 0x1F, 0x20, 0x40, 0x20, 0x1F }, /* V */
    { 0x7F, 0x20, 0x18, 0x20, 0x7F }, /* W */
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, /* X */
    { 0x03, 0x04, 0x78, 0x04, 0x03 }, /* Y */
    { 0x61, 0x51, 0x49, 0x45, 0x43 }, /* Z */
    { 0x00, 0x7F, 0x41, 0x41, 0x00 }, /* [ */
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, /* \ */
    { 0x00, 0x41, 0x41, 0x7F, 0x00 }, /* ] */
    { 0x04, 0x02, 0x01, 0x02, 0x04 }, /* ^ */
    { 0x40, 0x40, 0x40, 0x40, 0x40 }, /* _ */
    { 0x00, 0x01, 0x02, 0x04, 0x00 }, /* ` */
    { 0x20, 0x54, 0x54, 0x54, 0x78 }, /* a */
    { 0x7F, 0x48, 0x44, 0x44, 0x38 }, /* b */
    { 0x38, 0x44, 0x44, 0x44, 0x20 }, /* c */
    { 0x38, 0x44, 0x44, 0x48, 0x7F }, /* d */
    { 0x38, 0x54, 0x54, 0x54, 0x18 }, /* e */
    { 0x08, 0x7E, 0x09, 0x01, 0x02 }, /* f */
    { 0x08, 0x14, 0x54, 0x54, 0x3C }, /* g */
    { 0x7F, 0x08, 0x04, 0x04, 0x78 }, /* h */
    { 0x00, 0x44, 0x7D, 0x40, 0x00 }, /* i */
    { 0x20, 0x40, 0x44, 0x3D, 0x00 }, /* j */
    { 0x00, 0x7F, 0x10, 0x28, 0x44 }, /* k */
    { 0x00, 0x41, 0x7F, 0x40, 0x00 }, /* l */
    { 0x7C, 0x04, 0x18, 0x04, 0x78 }, /* m */
    { 0x7C, 0x08, 0x04, 0x04, 0x78 }, /* n */
    { 0x38, 0x44, 0x44, 0x44, 0x38 }, /* o */
    { 0x7C, 0x14, 0x14, 0x14, 0x08 }, /* p */
    { 0x08, 0x14, 0x14, 0x18, 0x7C }, /* q */
    { 0x7C, 0x08, 0x04, 0x04, 0x08 }, /* r */
    { 0x48, 0x54, 0x54, 0x54, 0x20 }, /* s */
    { 0x04, 0x3F, 0x44, 0x40, 0x20 }, /* t */
    { 0x3C, 0x40, 0x40, 0x20, 0x7C }, /* u */
    { 0x1C, 0x20, 0x40, 0x20, 0x1C }, /* v */
    { 0x3C, 0x40, 0x30, 0x40, 0x3C }, /* w */
    { 0x44, 0x28, 0x10, 0x28, 0x44 }, /* x */
    { 0x0C, 0x50, 0x50, 0x50, 0x3C }, /* y */
    { 0x44, 0x64, 0x54, 0x4C, 0x44 }, /* z */
    { 0x00, 0x08, 0x36, 0x41, 0x00 }, /* { */
    { 0x00, 0x00, 0x7F, 0x00, 0x00 }, /* | */
    { 0x00, 0x41, 0x36, 0x08, 0x00 }, /* } */
    { 0x08, 0x04, 0x08, 0x10, 0x08 }, /* ~ */
    { 0x00, 0x00, 0x00, 0x00, 0x00 }  /* DEL */
};

static void build_text_page(const char *src, uint8_t *out)
{
    size_t i;
    size_t n = strlen(src);

    memset(out, 0, OLED_CH1116_WIDTH);
    if (n > OLED_CH1116_LINE_CHARS) {
        n = OLED_CH1116_LINE_CHARS;
    }
    for (i = 0u; i < n; i++) {
        uint8_t c = (uint8_t)src[i];
        const uint8_t *g;

        if ((c < 0x20u) || (c > 0x7Fu)) {
            c = (uint8_t)'?';
        }
        g = s_font5x7[c - 0x20u];
        out[i * 6u + 0u] = g[0];
        out[i * 6u + 1u] = g[1];
        out[i * 6u + 2u] = g[2];
        out[i * 6u + 3u] = g[3];
        out[i * 6u + 4u] = g[4];
        /* column i*6+5 stays 0: the inter-character gap */
    }
}

/* Checkerboard: one lit pixel alternates on every row and every column. */
static void build_checker(uint8_t *out)
{
    uint8_t x;

    for (x = 0u; x < OLED_CH1116_WIDTH; x++) {
        out[x] = ((x & 1u) != 0u) ? 0xAAu : 0x55u;
    }
}

/* Deterministic `OLED frame` pattern. Every page buffer is built from scratch
 * (memset first) and all 8 pages are pushed in one JOB_FRAME run, so the result
 * does not depend on what was on the panel before - "clear, then draw" in one
 * pass instead of a separate 8-page clear plus 8-page draw.
 *
 *   page 0, page 7 : all 128 columns 0xFF -> solid top and bottom band, 8 px
 *   pages 1..6     : byte column lit at x = 0, 16, 32, 48, 64, 80, 96, 112
 *                    and 127 -> the 9 full-height vertical lines
 *
 * With the default column offset 2, x=0 lands on RAM column 2 and x=127 on 129,
 * both inside the visible glass window (RAM 2..129). If the left border is
 * missing the offset is too small; if the right border is missing it is too
 * large. */
static void build_frame_page(uint8_t page, uint8_t *out)
{
    uint8_t x;

    if ((page == 0u) || (page == (uint8_t)(OLED_CH1116_PAGES - 1u))) {
        memset(out, 0xFF, OLED_CH1116_WIDTH);   /* top and bottom border */
        return;
    }
    memset(out, 0, OLED_CH1116_WIDTH);
    for (x = 0u; x < OLED_CH1116_WIDTH; x++) {
        if ((x == 0u) || (x == (uint8_t)(OLED_CH1116_WIDTH - 1u)) ||
            ((x % 16u) == 0u)) {
            out[x] = 0xFFu;
        }
    }
}

int oled_ch1116_init(oled_ch1116_t *o, oled_ch1116_bus_fn bus, void *ctx)
{
    int rc;

    memset(o, 0, sizeof(*o));
    o->bus = bus;
    o->ctx = ctx;
    o->col_offset = (uint8_t)OLED_CH1116_COL_OFFSET;
    rc = bus(ctx, OLED_CH1116_CTRL_CMD, s_init_seq,
             (uint16_t)sizeof(s_init_seq));
    o->err = rc;
    return rc;
}

int oled_ch1116_display(oled_ch1116_t *o, int on)
{
    uint8_t cmd = (on != 0) ? 0xAFu : 0xAEu;
    int     rc  = o->bus(o->ctx, OLED_CH1116_CTRL_CMD, &cmd, 1u);

    if (rc != 0) {
        o->err = rc;
    }
    return rc;
}

void oled_ch1116_stage_line(oled_ch1116_t *o, uint8_t page, const char *text)
{
    size_t i = 0u;

    if (page >= OLED_CH1116_PAGES) {
        return;
    }
    while ((text[i] != '\0') && (i < OLED_CH1116_LINE_CHARS)) {
        o->line[page][i] = text[i];
        i++;
    }
    o->line[page][i] = '\0';
}

int oled_ch1116_start_pattern(oled_ch1116_t *o, uint8_t pattern)
{
    if (o->job != OLED_CH1116_JOB_NONE) {
        return -1;
    }
    o->job       = (uint8_t)OLED_CH1116_JOB_PATTERN;
    o->pattern   = pattern;
    o->next_page = 0u;
    o->end_page  = (uint8_t)OLED_CH1116_PAGES;
    o->err       = 0;
    return 0;
}

int oled_ch1116_start_text(oled_ch1116_t *o, uint8_t first_page, uint8_t count)
{
    if (o->job != OLED_CH1116_JOB_NONE) {
        return -1;
    }
    if ((first_page >= OLED_CH1116_PAGES) || (count == 0u) ||
        ((uint16_t)first_page + count > OLED_CH1116_PAGES)) {
        return -1;
    }
    o->job       = (uint8_t)OLED_CH1116_JOB_TEXT;
    o->next_page = first_page;
    o->end_page  = (uint8_t)(first_page + count);
    o->err       = 0;
    return 0;
}

int oled_ch1116_start_frame(oled_ch1116_t *o)
{
    if (o->job != OLED_CH1116_JOB_NONE) {
        return -1;
    }
    o->job       = (uint8_t)OLED_CH1116_JOB_FRAME;
    o->next_page = 0u;
    o->end_page  = (uint8_t)OLED_CH1116_PAGES;
    o->err       = 0;
    return 0;
}

void oled_ch1116_set_col_offset(oled_ch1116_t *o, uint8_t col)
{
    o->col_offset = col;
}

uint8_t oled_ch1116_col_offset(const oled_ch1116_t *o)
{
    return o->col_offset;
}

int oled_ch1116_service(oled_ch1116_t *o)
{
    uint8_t cmds[3];
    uint8_t page_buf[OLED_CH1116_WIDTH];
    uint8_t col = (uint8_t)(o->col_offset & 0x7Fu);
    int     rc;

    if (o->job == (uint8_t)OLED_CH1116_JOB_NONE) {
        return 0;
    }

    /* Page addressing mode: B0|page, then column start split into nibbles. */
    cmds[0] = (uint8_t)(0xB0u | (o->next_page & 0x07u));
    cmds[1] = (uint8_t)(0x00u | (col & 0x0Fu));
    cmds[2] = (uint8_t)(0x10u | ((col >> 4) & 0x0Fu));
    rc = o->bus(o->ctx, OLED_CH1116_CTRL_CMD, cmds, 3u);

    if (rc == 0) {
        if (o->job == (uint8_t)OLED_CH1116_JOB_PATTERN) {
            if (o->pattern == 0xAAu) {
                build_checker(page_buf);
            } else {
                memset(page_buf, o->pattern, sizeof(page_buf));
            }
        } else if (o->job == (uint8_t)OLED_CH1116_JOB_FRAME) {
            build_frame_page(o->next_page, page_buf);
        } else {
            build_text_page(o->line[o->next_page], page_buf);
        }
        rc = o->bus(o->ctx, OLED_CH1116_CTRL_DATA, page_buf,
                    (uint16_t)sizeof(page_buf));
    }
    if (rc != 0) {
        o->err = rc;
        o->job = (uint8_t)OLED_CH1116_JOB_NONE;
        return 0;
    }

    o->next_page++;
    if (o->next_page >= o->end_page) {
        o->job = (uint8_t)OLED_CH1116_JOB_NONE;
        return 0;
    }
    return 1;
}

void oled_ch1116_abort(oled_ch1116_t *o)
{
    o->job       = (uint8_t)OLED_CH1116_JOB_NONE;
    o->next_page = 0u;
    o->end_page  = 0u;
}

int oled_ch1116_busy(const oled_ch1116_t *o)
{
    return (o->job != (uint8_t)OLED_CH1116_JOB_NONE) ? 1 : 0;
}

int oled_ch1116_err(const oled_ch1116_t *o)
{
    return o->err;
}
