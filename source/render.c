#include "render.h"

#include <string.h>

#include "font.h"

const Color ttx_palette[8] = {
    0x000000, 0xFF0000, 0x00FF00, 0xFFFF00,
    0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
};


void surf_fill(const Surface *s, int x, int y, int w, int h, Color c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0)
        return;
    uint8_t b = c & 0xFF, g = (c >> 8) & 0xFF, r = (c >> 16) & 0xFF;
    for (int j = 0; j < h; j++) {
        uint8_t *p = s->px + s->base + x * s->xstride + (y + j) * s->ystride;
        for (int i = 0; i < w; i++, p += s->xstride) {
            p[0] = b;
            p[1] = g;
            p[2] = r;
        }
    }
}

void draw_glyph(const Surface *s, int x, int y, uint8_t ch, Color fg, int sx, int sy)
{
    if (ch < 0x20 || ch > 0x7F)
        return;
    const uint8_t *g = ttx_font[ch - 0x20];
    for (int row = 0; row < 9; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x10 >> col))
                surf_fill(s, x + (1 + col) * sx, y + row * sy, sx, sy, fg);
        }
    }
}

int draw_text(const Surface *s, int x, int y, const char *text, Color fg, int sx, int sy)
{
    for (; *text; text++, x += CELL_W * sx)
        draw_glyph(s, x, y, (uint8_t)*text, fg, sx, sy);
    return x;
}

int text_width(const char *text, int sx)
{
    return (int)strlen(text) * CELL_W * sx;
}

// Sextant bits: 0,1 top; 2,3 middle; 4,6 bottom.
static void draw_mosaic(const Surface *s, int x, int y, int h, uint8_t ch, int separated, int sy, Color fg)
{
    static const uint8_t bit[6] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x40};
    int ys[4] = {0, (h + 1) / 3, (2 * h + 1) / 3, h};
    int xs[3] = {0, CELL_W / 2, CELL_W};
    for (int i = 0; i < 6; i++) {
        if (!(ch & bit[i]))
            continue;
        int x0 = xs[i & 1], x1 = xs[(i & 1) + 1];
        int y0 = ys[i >> 1], y1 = ys[(i >> 1) + 1];
        if (separated) {
            x0 += 1;
            y1 -= sy;
        }
        surf_fill(s, x + x0, y + y0, x1 - x0, y1 - y0, fg);
    }
}

static void draw_fg(const Surface *s, int x, int y, int h, uint8_t ch, int mosaic, int separated,
                    Color fg, int sy)
{
    if (ch == ' ')
        return;
    if (mosaic && (ch & 0x20))
        draw_mosaic(s, x, y, h, ch, separated, sy, fg);
    else
        draw_glyph(s, x, y, ch, fg, 1, sy);
}

typedef struct {
    uint8_t ch, mosaic, sep, dbl;
    Color fg;
} FgCell;

// Returns nonzero if the row is double height and covers the next one.
static int render_row(const Surface *s, const uint8_t *row, int y, int h1, int h2, int allow_dbl,
                      int reveal, int flash_on)
{
    FgCell cells[TTX_COLS];
    int dbl_row = allow_dbl && memchr(row, 0x0D, TTX_COLS) != NULL;
    int fg = 7, bg = 0, mosaic = 0, sep = 0, flash = 0, conceal = 0, dbl = 0, hold = 0;
    uint8_t held = ' ';
    int held_sep = 0;

    for (int c = 0; c < TTX_COLS; c++) {
        uint8_t b = row[c] & 0x7F;

        // set-at attributes
        switch (b) {
        case 0x09: flash = 0; break;
        case 0x0C: if (dbl) { dbl = 0; held = ' '; } break;
        case 0x18: conceal = 1; break;
        case 0x19: sep = 0; break;
        case 0x1A: sep = 1; break;
        case 0x1C: bg = 0; break;
        case 0x1D: bg = fg; break;
        case 0x1E: hold = 1; break;
        }

        uint8_t ch = b;
        int is_mosaic = 0, cell_sep = sep;
        if (b < 0x20) {
            ch = ' ';
            if (hold && mosaic) {
                ch = held;
                is_mosaic = 1;
                cell_sep = held_sep;
            }
        } else if (mosaic && (b & 0x20)) {
            is_mosaic = 1;
            held = b;
            held_sep = sep;
        }
        if ((flash && !flash_on) || (conceal && !reveal))
            ch = ' ';

        surf_fill(s, c * CELL_W, y, CELL_W, dbl_row ? h1 + h2 : h1, ttx_palette[bg]);
        cells[c] = (FgCell){ch, (uint8_t)is_mosaic, (uint8_t)cell_sep, (uint8_t)(dbl_row && dbl),
                            ttx_palette[fg]};

        // set-after attributes
        if (b <= 0x07) {
            fg = b;
            if (mosaic) { mosaic = 0; held = ' '; }
            conceal = 0;
        } else if (b == 0x08) {
            flash = 1;
        } else if (b == 0x0D) {
            if (allow_dbl && !dbl) { dbl = 1; held = ' '; }
        } else if (b >= 0x10 && b <= 0x17) {
            fg = b - 0x10;
            if (!mosaic) { mosaic = 1; held = ' '; }
            conceal = 0;
        } else if (b == 0x1F) {
            hold = 0;
        }
    }

    for (int c = 0; c < TTX_COLS; c++) {
        const FgCell *k = &cells[c];
        draw_fg(s, c * CELL_W, y, k->dbl ? h1 + h2 : h1, k->ch, k->mosaic, k->sep, k->fg,
                k->dbl ? 2 : 1);
    }
    return dbl_row;
}

static int row_y(const Surface *s, int r)
{
    return r * s->h / TTX_ROWS;
}

void ttx_render(const Surface *s, const uint8_t rows[TTX_ROWS][TTX_COLS], int reveal, int flash_on)
{
    for (int r = 0; r < TTX_ROWS; r++) {
        int y = row_y(s, r);
        int h1 = row_y(s, r + 1) - y;
        int allow_dbl = r >= 1 && r <= 22;
        int h2 = allow_dbl ? row_y(s, r + 2) - row_y(s, r + 1) : 0;
        if (render_row(s, rows[r], y, h1, h2, allow_dbl, reveal, flash_on))
            r++;
    }
}

void surf_blit_nonblack(const Surface *dst, int x, int y, const Surface *src)
{
    for (int j = 0; j < src->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= dst->h)
            continue;
        const uint8_t *sp = src->px + src->base + j * src->ystride;
        for (int i = 0; i < src->w; i++, sp += src->xstride) {
            int dx = x + i;
            if ((sp[0] | sp[1] | sp[2]) == 0 || dx < 0 || dx >= dst->w)
                continue;
            uint8_t *dp = dst->px + dst->base + dx * dst->xstride + dy * dst->ystride;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
        }
    }
}
