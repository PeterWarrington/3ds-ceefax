// Saves both screens, laid out as on the console: ./screenshot page[/sub] out.bmp [region]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../source/app.h"
#include "../source/fetcher.h"
#include "../source/platform.h"
#include "bmp.h"

#define W 400
#define H 480

static uint8_t top_px[400 * 240 * 3], bot_px[320 * 240 * 3], out_px[W * H * 3];

static void frame_input(uint32_t down)
{
    static const Surface top = {top_px, 400, 240, 3, 400 * 3, 0};
    static const Surface bot = {bot_px, 320, 240, 3, 320 * 3, 0};
    AppInput in = {down, 0, 0, 0};
    time_t t = time(NULL);
    app_update(&in, plat_now_ms(), localtime(&t));
    app_draw(&top, NULL, &bot, 0);
    plat_sleep_ms(16);
}

static void frame(void)
{
    frame_input(0);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s page[/sub] out.bmp [region]\n", argv[0]);
        return 1;
    }
    char *slash;
    AppConfig cfg = {0, (int)strtol(argv[1], &slash, 16), 1};
    int sub = *slash == '/' ? atoi(slash + 1) : 1;
    for (int i = 0; argc > 3 && i < fetch_region_count; i++)
        if (!strcmp(argv[3], fetch_regions[i].dir))
            cfg.region = i;
    if (app_init(&cfg, "/dev/null"))
        return 1;

    for (int i = 0; i < 600 && fetcher_state(cfg.page) != PAGE_READY; i++)
        frame();
    for (int i = 0; i < 10; i++)
        frame();
    for (int i = 1; i < sub; i++)
        frame_input(IN_RIGHT);
    frame();
    if (fetcher_state(cfg.page) != PAGE_READY) {
        fprintf(stderr, "page %s did not load\n", argv[1]);
        return 1;
    }

    memset(out_px, 0, sizeof(out_px));
    for (int y = 0; y < 240; y++) {
        memcpy(out_px + y * W * 3, top_px + y * 400 * 3, 400 * 3);
        memcpy(out_px + ((240 + y) * W + 40) * 3, bot_px + y * 320 * 3, 320 * 3);
    }
    write_bmp(argv[2], out_px, W, H, 2);
    app_exit();
    return 0;
}
