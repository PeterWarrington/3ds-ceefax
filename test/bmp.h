// Minimal BMP writer (BGR input, integer upscale).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bmp_put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static void write_bmp(const char *path, const uint8_t *bgr, int w, int h, int scale)
{
    int ow = w * scale, oh = h * scale, stride = ow * 3;
    uint8_t hdr[54] = {'B', 'M'};
    bmp_put32(hdr + 2, 54 + (uint32_t)(stride * oh));
    bmp_put32(hdr + 10, 54);
    bmp_put32(hdr + 14, 40);
    bmp_put32(hdr + 18, (uint32_t)ow);
    bmp_put32(hdr + 22, (uint32_t)oh);
    hdr[26] = 1;
    hdr[28] = 24;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(hdr, 1, 54, f);
    uint8_t *line = malloc((size_t)stride);
    for (int y = oh - 1; y >= 0; y--) {
        for (int x = 0; x < ow; x++)
            memcpy(line + x * 3, bgr + ((y / scale) * w + x / scale) * 3, 3);
        fwrite(line, 1, (size_t)stride, f);
    }
    free(line);
    fclose(f);
}
