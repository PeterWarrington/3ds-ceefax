// Platform-independent application logic.
#pragma once
#include <stdint.h>
#include <time.h>

#include "render.h"

enum {
    IN_UP = 1 << 0,
    IN_DOWN = 1 << 1,
    IN_LEFT = 1 << 2,
    IN_RIGHT = 1 << 3,
    IN_A = 1 << 4,
    IN_B = 1 << 5,
    IN_X = 1 << 6,
    IN_Y = 1 << 7,
    IN_L = 1 << 8,
    IN_R = 1 << 9,
    IN_SELECT = 1 << 10,
    IN_START = 1 << 11,
};

typedef struct {
    uint32_t down;      // IN_* pressed this frame
    int touching;
    int tx, ty;
} AppInput;

typedef struct {
    int region;
    int page;
    int verify_tls;
} AppConfig;

void app_config_load(const char *path, AppConfig *cfg);
void app_config_save(const char *path, const AppConfig *cfg);

int app_init(const AppConfig *cfg, const char *config_path);
// Returns 0 when the user asked to quit.
int app_update(const AppInput *in, uint64_t now_ms, const struct tm *local);
int app_bottom_on(void);
// Redraws changed screens; returns bit 0 for top, bit 1 for bottom.
// depth > 0 draws a stereo pair with `depth` px pop-out per eye.
int app_draw(const Surface *top_left, const Surface *top_right, const Surface *bottom, int depth);
void app_exit(void);
