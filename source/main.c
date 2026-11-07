// 3DS entry point.
#include <3ds.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000
#define CONFIG_DIR "sdmc:/3ds"
#define CONFIG_PATH "sdmc:/3ds/ceefax.cfg"

// Back buffers in the framebuffers' native (rotated) layout.
static uint8_t top_px[400 * 240 * 3];
static uint8_t top_right_px[400 * 240 * 3];
static uint8_t bot_px[320 * 240 * 3];

static Surface fb_surface(uint8_t *px, int width)
{
    return (Surface){px, width, 240, 240 * 3, -3, 239 * 3};
}

// Logs to `3dslink -s` when launched that way.
static int linked;
#define LOG(...) do { if (linked) { printf(__VA_ARGS__); fflush(stdout); } } while (0)

static int backlight_on = 1;
static volatile int resumed;

// Holding a gsp::Lcd session open freezes the HOME menu.
static void set_bottom_backlight(int on)
{
    if (on == backlight_on || R_FAILED(gspLcdInit()))
        return;
    if (on)
        GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
    else
        GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
    gspLcdExit();
    backlight_on = on;
}

static void on_apt(APT_HookType hook, void *param)
{
    (void)param;
    LOG("apt hook %d\n", (int)hook);
    if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT)
        set_bottom_backlight(1);
    if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP)
        resumed = 1;
}

static uint32_t map_keys(u32 k)
{
    uint32_t in = 0;
    if (k & KEY_UP) in |= IN_UP;
    if (k & KEY_DOWN) in |= IN_DOWN;
    if (k & KEY_LEFT) in |= IN_LEFT;
    if (k & KEY_RIGHT) in |= IN_RIGHT;
    if (k & KEY_A) in |= IN_A;
    if (k & KEY_B) in |= IN_B;
    if (k & KEY_X) in |= IN_X;
    if (k & KEY_Y) in |= IN_Y;
    if (k & KEY_L) in |= IN_L;
    if (k & KEY_R) in |= IN_R;
    if (k & KEY_SELECT) in |= IN_SELECT;
    if (k & KEY_START) in |= IN_START;
    return in;
}

int main(void)
{
    gfxInitDefault();
    romfsInit();
    osSetSpeedupEnable(true);
    aptHookCookie apt_cookie;
    aptHook(&apt_cookie, on_apt, NULL);

    u32 *soc_buf = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    int soc_ok = soc_buf && R_SUCCEEDED(socInit(soc_buf, SOC_BUFFERSIZE));
    if (soc_ok && __3dslink_host.s_addr)
        linked = link3dsStdio() >= 0;
    LOG("ceefax started\n");

    mkdir(CONFIG_DIR, 0777);
    AppConfig cfg;
    app_config_load(CONFIG_PATH, &cfg);

    Surface top = fb_surface(top_px, 400);
    Surface top_right = fb_surface(top_right_px, 400);
    int stereo = 0;
    Surface bot = fb_surface(bot_px, 320);
    int top_copies = 0, bot_copies = 0;

    uint32_t frame = 0;
    if (app_init(&cfg, CONFIG_PATH) == 0) {
        while (aptMainLoop()) {
            if (++frame % 300 == 0)
                LOG("frame %lu\n", (unsigned long)frame);
            if (resumed) {
                resumed = 0;
                top_copies = bot_copies = 2;
            }
            hidScanInput();
            AppInput in = {0};
            in.down = map_keys(hidKeysDown());
            if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition t;
                hidTouchRead(&t);
                in.touching = 1;
                in.tx = t.px;
                in.ty = t.py;
            }

            // The 3DS RTC holds local time.
            time_t now = time(NULL);
            if (!app_update(&in, osGetTime(), gmtime(&now)))
                break;

            set_bottom_backlight(app_bottom_on());

            // 3D slider: 0-3px per eye
            int depth = (int)ceilf(osGet3DSliderState() * 3.0f);
            if ((depth > 0) != stereo) {
                stereo = depth > 0;
                gfxSet3D(stereo);
            }

            int drawn = app_draw(&top, &top_right, &bot, depth);
            if (drawn & 1) top_copies = 2; // both buffers of the double-buffered pair
            if (drawn & 2) bot_copies = 2;
            if (top_copies) {
                memcpy(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), top_px, sizeof(top_px));
                if (stereo)
                    memcpy(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), top_right_px,
                           sizeof(top_right_px));
                top_copies--;
            }
            if (bot_copies) {
                memcpy(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), bot_px, sizeof(bot_px));
                bot_copies--;
            }
            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();
        }
        LOG("main loop ended, shutting down\n");
        app_exit();
        LOG("app_exit done\n");
    }

    aptUnhook(&apt_cookie);
    set_bottom_backlight(1);
    if (soc_ok)
        socExit();
    free(soc_buf);
    romfsExit();
    gfxExit();
    return 0;
}
