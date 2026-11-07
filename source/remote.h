// Bottom-screen remote control.
#pragma once
#include <stdint.h>

#include "render.h"

typedef enum {
    ACT_NONE,
    ACT_DIGIT,     // arg = 0-9
    ACT_PAGE_PREV,
    ACT_PAGE_NEXT,
    ACT_SUB_PREV,
    ACT_SUB_NEXT,
    ACT_INDEX,
    ACT_HOLD,
    ACT_REVEAL,
    ACT_REGION,
    ACT_FASTEXT,   // arg = 0-3 (red, green, yellow, cyan)
    ACT_SCREEN_OFF,
} Action;

typedef struct {
    Action act;
    int arg;
} Press;

typedef struct {
    const char *page_label;   // "P100", or "P12-" while typing
    const char *region;
    const char *info;
    const char *status;
    Color status_color;
    int hold, reveal;
    uint16_t links[4];
    int pressed;              // button index being touched, or -1
} RemoteView;

void remote_draw(const Surface *s, const RemoteView *v);
int remote_hit(int x, int y);
Press remote_press(int button);
