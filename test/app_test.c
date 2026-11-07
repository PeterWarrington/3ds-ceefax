// Drives the app against the live feed and saves screenshots: ./app_test outdir
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../source/app.h"
#include "../source/platform.h"
#include "bmp.h"

static uint8_t top_px[400 * 240 * 3], top_r_px[400 * 240 * 3], bot_px[320 * 240 * 3];
static const Surface top = {top_px, 400, 240, 3, 400 * 3, 0};
static const Surface top_r = {top_r_px, 400, 240, 3, 400 * 3, 0};
static const Surface bot = {bot_px, 320, 240, 3, 320 * 3, 0};
static const char *outdir;

static void run(int frames, AppInput in)
{
    for (int i = 0; i < frames; i++) {
        time_t t = time(NULL);
        app_update(&in, plat_now_ms(), localtime(&t));
        app_draw(&top, &top_r, &bot, 3);
        in.down = 0;
        plat_sleep_ms(16);
    }
}

static void tap(int x, int y)
{
    AppInput in = {0, 1, x, y};
    run(2, in);
    AppInput up = {0};
    run(1, up);
}

static void shot(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_top.bmp", outdir, name);
    write_bmp(path, top_px, 400, 240, 2);
    // red/cyan anaglyph
    static uint8_t ana[400 * 240 * 3];
    for (int i = 0; i < 400 * 240; i++) {
        ana[i * 3 + 0] = top_r_px[i * 3 + 0];
        ana[i * 3 + 1] = top_r_px[i * 3 + 1];
        ana[i * 3 + 2] = top_px[i * 3 + 2];
    }
    snprintf(path, sizeof(path), "%s/%s_anaglyph.bmp", outdir, name);
    write_bmp(path, ana, 400, 240, 2);
    snprintf(path, sizeof(path), "%s/%s_bot.bmp", outdir, name);
    write_bmp(path, bot_px, 320, 240, 2);
    printf("shot %s\n", name);
}

int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : ".";
    char cfgpath[512];
    snprintf(cfgpath, sizeof(cfgpath), "%s/ceefax.cfg", outdir);
    remove(cfgpath);
    AppConfig cfg;
    app_config_load(cfgpath, &cfg);
    if (app_init(&cfg, cfgpath))
        return 1;
    AppInput none = {0};

    run(5, none);
    shot("0_start");
    run(180, none);
    shot("1_p100");

    tap(10, 60);  // 1
    tap(80, 170); // 0
    tap(10, 90);  // 4  -> P104
    run(1, none);
    shot("2_typed");
    run(120, none);
    shot("3_p104");

    tap(90, 210); // green fastext
    run(120, none);
    shot("4_fastext");

    tap(10, 130); // 7
    tap(80, 130); // 8
    tap(80, 130); // 8  -> P788
    run(60, none);
    shot("5_missing");

    tap(210, 170); // REGION -> East
    tap(210, 60);  // INDEX
    run(180, none);
    shot("6_region");
    app_exit();
    FILE *f = fopen(cfgpath, "r");
    char buf[256] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    printf("saved config:\n%s", buf);
    return 0;
}
