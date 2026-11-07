// Renders each subpage of a TTI file to BMP: ./render_test page.tti outprefix
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/render.h"
#include "../source/ttx.h"
#include "bmp.h"

#define W 400
#define H 240

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s page.tti outprefix\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    TtxPage page;
    if (ttx_parse(buf, n, &page)) {
        fprintf(stderr, "parse failed\n");
        return 1;
    }
    printf("page %03X, %d subpages\n", page.number, page.count);

    static uint8_t px[W * H * 3];
    Surface s = {px, W, H, 3, W * 3, 0};
    for (int i = 0; i < page.count; i++) {
        TtxSubpage *sp = &page.subs[i];
        char hdr[41];
        snprintf(hdr, sizeof(hdr), "P%03X    CEEFAX 1 %03X Sat 07 Nov\00319:23/45", page.number, page.number);
        memcpy(sp->rows[0], hdr, 40);
        printf("  sub %d cycle %ds links %03X %03X %03X %03X %03X\n", i + 1, sp->cycle_secs,
               sp->links[0], sp->links[1], sp->links[2], sp->links[3], sp->links[5]);
        ttx_render(&s, (const uint8_t(*)[TTX_COLS])sp->rows, 0, 1);
        char path[512];
        snprintf(path, sizeof(path), "%s_%d.bmp", argv[2], i + 1);
        write_bmp(path, px, W, H, 2);
    }
    ttx_free(&page);
    return 0;
}
