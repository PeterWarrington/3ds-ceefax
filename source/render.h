// Software renderer for teletext pages and the remote UI.
#pragma once
#include <stdint.h>

#include "ttx.h"

// BGR8 pixel buffer with arbitrary strides (e.g. the rotated 3DS framebuffer).
typedef struct {
    uint8_t *px;
    int w, h;                 // logical (on-screen) size
    int xstride, ystride;     // byte offset per +1 x / +1 y
    int base;                 // byte offset of (0,0)
} Surface;

typedef uint32_t Color;       // 0xRRGGBB

extern const Color ttx_palette[8];

// One pixel per SAA5050 dot; rows alternate 9/10px to fill 240 lines.
#define CELL_W 6
#define PAGE_W (CELL_W * TTX_COLS)

void surf_fill(const Surface *s, int x, int y, int w, int h, Color c);
// Draws a G0 glyph (0x20-0x7F), foreground only, at integer scale.
void draw_glyph(const Surface *s, int x, int y, uint8_t ch, Color fg, int sx, int sy);
int draw_text(const Surface *s, int x, int y, const char *text, Color fg, int sx, int sy);
int text_width(const char *text, int sx);

void ttx_render(const Surface *s, const uint8_t rows[TTX_ROWS][TTX_COLS], int reveal, int flash_on);
// Copies src to dst at (x, y), treating black as transparent.
void surf_blit_nonblack(const Surface *dst, int x, int y, const Surface *src);
