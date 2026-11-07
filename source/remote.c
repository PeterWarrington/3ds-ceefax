#include "remote.h"

#include <stdio.h>
#include <string.h>

#define BLACK   0x000000
#define RED     0xFF0000
#define GREEN   0x00FF00
#define YELLOW  0xFFFF00
#define BLUE    0x0000FF
#define CYAN    0x00FFFF
#define WHITE   0xFFFFFF
#define GREY    0x404040

typedef struct {
    int x, y, w, h;
    const char *label;   // NULL: drawn from view state (fastext)
    Press press;
} Button;

// '[' and ']' are arrows in the teletext character set.
static const Button buttons[] = {
    // keypad
    {  4,  50, 60, 32, "1", {ACT_DIGIT, 1}},
    { 70,  50, 60, 32, "2", {ACT_DIGIT, 2}},
    {136,  50, 60, 32, "3", {ACT_DIGIT, 3}},
    {  4,  88, 60, 32, "4", {ACT_DIGIT, 4}},
    { 70,  88, 60, 32, "5", {ACT_DIGIT, 5}},
    {136,  88, 60, 32, "6", {ACT_DIGIT, 6}},
    {  4, 126, 60, 32, "7", {ACT_DIGIT, 7}},
    { 70, 126, 60, 32, "8", {ACT_DIGIT, 8}},
    {136, 126, 60, 32, "9", {ACT_DIGIT, 9}},
    {  4, 164, 60, 32, "P-", {ACT_PAGE_PREV, 0}},
    { 70, 164, 60, 32, "0", {ACT_DIGIT, 0}},
    {136, 164, 60, 32, "P+", {ACT_PAGE_NEXT, 0}},
    // function keys
    {204,  50, 112, 32, "INDEX", {ACT_INDEX, 0}},
    {204,  88,  53, 32, "HOLD", {ACT_HOLD, 0}},
    {263,  88,  53, 32, "REVL", {ACT_REVEAL, 0}},
    {204, 126,  53, 32, "[SUB", {ACT_SUB_PREV, 0}},
    {263, 126,  53, 32, "SUB]", {ACT_SUB_NEXT, 0}},
    {204, 164, 112, 32, "REGION", {ACT_REGION, 0}},
    {276,  22,  40, 22, "OFF", {ACT_SCREEN_OFF, 0}},
    // fastext
    {  4, 204, 73, 32, NULL, {ACT_FASTEXT, 0}},
    { 83, 204, 73, 32, NULL, {ACT_FASTEXT, 1}},
    {163, 204, 73, 32, NULL, {ACT_FASTEXT, 2}},
    {243, 204, 73, 32, NULL, {ACT_FASTEXT, 3}},
};
#define NBUTTONS (int)(sizeof(buttons) / sizeof(buttons[0]))

static const Color fastext_colors[4] = {RED, GREEN, YELLOW, CYAN};

static void draw_label(const Surface *s, const Button *b, const char *text, Color fg)
{
    int x = b->x + (b->w - text_width(text, 2)) / 2;
    int y = b->y + (b->h - 18) / 2;
    draw_text(s, x, y, text, fg, 2, 2);
}

static void draw_button(const Surface *s, const RemoteView *v, int i)
{
    const Button *b = &buttons[i];
    Color bg = BLUE, fg = WHITE;
    char text[8];
    const char *label = b->label;

    if (b->press.act == ACT_FASTEXT) {
        uint16_t link = v->links[b->press.arg];
        bg = fastext_colors[b->press.arg];
        fg = BLACK;
        if (link >= 0x100 && link < TTX_NO_LINK) {
            snprintf(text, sizeof(text), "%03X", link);
        } else {
            snprintf(text, sizeof(text), "---");
            bg = GREY;
        }
        label = text;
    } else if (b->press.act == ACT_DIGIT) {
        fg = YELLOW;
    } else if ((b->press.act == ACT_HOLD && v->hold) || (b->press.act == ACT_REVEAL && v->reveal)) {
        bg = YELLOW;
        fg = BLACK;
    } else if (b->press.act == ACT_SCREEN_OFF) {
        bg = 0x404040;
        fg = WHITE;
    } else if (b->press.act != ACT_PAGE_PREV && b->press.act != ACT_PAGE_NEXT) {
        fg = CYAN;
    }
    if (i == v->pressed) {
        bg = WHITE;
        fg = BLACK;
    }
    surf_fill(s, b->x, b->y, b->w, b->h, bg);
    draw_label(s, b, label, fg);
}

void remote_draw(const Surface *s, const RemoteView *v)
{
    surf_fill(s, 0, 0, s->w, s->h, BLACK);

    draw_text(s, 4, 3, v->page_label, YELLOW, 2, 2);
    draw_text(s, s->w - 4 - text_width(v->region, 2), 3, v->region, CYAN, 2, 2);
    draw_text(s, 4, 25, v->info, WHITE, 1, 1);
    char status[46];
    snprintf(status, sizeof(status), "%s", v->status);
    draw_text(s, 4, 36, status, v->status_color, 1, 1);
    surf_fill(s, 0, 46, s->w, 1, BLUE);

    for (int i = 0; i < NBUTTONS; i++)
        draw_button(s, v, i);
}

int remote_hit(int x, int y)
{
    for (int i = 0; i < NBUTTONS; i++) {
        const Button *b = &buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
            return i;
    }
    return -1;
}

Press remote_press(int button)
{
    Press none = {ACT_NONE, 0};
    return (button >= 0 && button < NBUTTONS) ? buttons[button].press : none;
}
