#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fetcher.h"
#include "remote.h"
#include "ttx.h"

#define HISTORY 32
#define ROLL_MS 60

static struct {
    AppConfig cfg;
    const char *config_path;

    int target;          // page the viewer asked for
    uint32_t version;    // cache version of `target` we've taken
    int shown;           // page number currently in `page`, 0 if none
    TtxPage page;
    int sub;
    uint64_t sub_since;
    int hold, reveal;

    char entry[4];       // digits typed so far
    int entry_len;
    int history[HISTORY];
    int history_n;

    int roll;            // header page counter while searching
    uint64_t roll_at;
    int touch_button;
    int bottom_off;
    int depth;
    int flash_on;
    int second;

    char status[64];
    Color status_color;
    int top_dirty, bottom_dirty;
    uint64_t now;
    struct tm tm;
} A;

static int valid_page(int p)
{
    return p >= 0x100 && p <= 0x8FF;
}


void app_config_load(const char *path, AppConfig *cfg)
{
    cfg->region = 0;
    cfg->page = 0x100;
    cfg->verify_tls = 1;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *val = eq + 1;
        val[strcspn(val, "\r\n")] = 0;
        if (!strcmp(line, "region")) {
            for (int i = 0; i < fetch_region_count; i++)
                if (!strcmp(val, fetch_regions[i].dir))
                    cfg->region = i;
        } else if (!strcmp(line, "page")) {
            int p = (int)strtol(val, NULL, 16);
            if (valid_page(p))
                cfg->page = p;
        } else if (!strcmp(line, "verify_tls")) {
            cfg->verify_tls = atoi(val) != 0;
        }
    }
    fclose(f);
}

void app_config_save(const char *path, const AppConfig *cfg)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "region=%s\npage=%03X\nverify_tls=%d\n", fetch_regions[cfg->region].dir, cfg->page,
            cfg->verify_tls);
    fclose(f);
}


static const TtxSubpage *current_sub(void)
{
    return A.page.count ? &A.page.subs[A.sub] : NULL;
}

static void update_prefetch(void)
{
    int pages[8], n = 0;
    const TtxSubpage *s = current_sub();
    if (s)
        for (int i = 0; i < 4; i++)
            pages[n++] = s->links[i];
    pages[n++] = fetcher_step(A.target, 1);
    pages[n++] = fetcher_step(A.target, -1);
    pages[n++] = 0x100;
    fetcher_prefetch(pages, n);
}

static void poll_page(void)
{
    TtxPage fresh;
    if (!fetcher_get(A.target, &fresh, &A.version))
        return;
    if (A.shown != A.target) {
        A.sub = 0;
        A.sub_since = A.now;
    } else if (A.sub >= fresh.count) {
        A.sub = 0;
    }
    ttx_free(&A.page);
    A.page = fresh;
    A.shown = A.target;
    A.top_dirty = A.bottom_dirty = 1;
    update_prefetch();
}

static void goto_page(int p, int remember)
{
    if (!valid_page(p))
        return;
    if (remember && p != A.target && valid_page(A.target)) {
        if (A.history_n == HISTORY) {
            memmove(A.history, A.history + 1, sizeof(A.history) - sizeof(A.history[0]));
            A.history_n--;
        }
        A.history[A.history_n++] = A.target;
    }
    A.target = p;
    A.version = 0;
    A.entry_len = 0;
    A.reveal = 0;
    A.hold = 0;
    A.roll = A.shown ? A.shown : p;
    A.cfg.page = p;
    fetcher_want(p);
    poll_page();
    update_prefetch();
    A.top_dirty = A.bottom_dirty = 1;
}

// Next/previous existing page; counts in decimal until the index loads.
static void step_page(int dir)
{
    int p = fetcher_step(A.target, dir);
    if (p < 0) {
        int dec = (A.target >> 8) * 100 + ((A.target >> 4) & 0xF) * 10 + (A.target & 0xF);
        dec += dir;
        if (dec < 100)
            dec = 899;
        if (dec > 899)
            dec = 100;
        p = ((dec / 100) << 8) | (((dec / 10) % 10) << 4) | (dec % 10);
    }
    goto_page(p, 1);
}

static void step_sub(int dir)
{
    if (A.page.count < 2)
        return;
    A.sub = (A.sub + dir + A.page.count) % A.page.count;
    A.sub_since = A.now;
    A.top_dirty = A.bottom_dirty = 1;
}

static void key_digit(int d)
{
    if (A.entry_len == 0 && (d < 1 || d > 8))
        return; // magazines are 1-8
    A.entry[A.entry_len++] = (char)('0' + d);
    A.entry[A.entry_len] = 0;
    if (A.entry_len == 3)
        goto_page((int)strtol(A.entry, NULL, 16), 1);
    A.top_dirty = A.bottom_dirty = 1;
}

static void change_region(int dir)
{
    A.cfg.region = (A.cfg.region + dir + fetch_region_count) % fetch_region_count;
    fetcher_set_region(A.cfg.region);
    ttx_free(&A.page);
    A.shown = 0;
    goto_page(A.target, 0);
    app_config_save(A.config_path, &A.cfg);
}

static void do_action(Press p)
{
    const TtxSubpage *s = current_sub();
    switch (p.act) {
    case ACT_DIGIT: key_digit(p.arg); break;
    case ACT_PAGE_PREV: step_page(-1); break;
    case ACT_PAGE_NEXT: step_page(1); break;
    case ACT_SUB_PREV: step_sub(-1); break;
    case ACT_SUB_NEXT: step_sub(1); break;
    case ACT_INDEX: goto_page(0x100, 1); break;
    case ACT_HOLD:
        A.hold = !A.hold;
        A.sub_since = A.now;
        A.top_dirty = A.bottom_dirty = 1;
        break;
    case ACT_REVEAL:
        A.reveal = !A.reveal;
        A.top_dirty = A.bottom_dirty = 1;
        break;
    case ACT_REGION: change_region(1); break;
    case ACT_SCREEN_OFF: A.bottom_off = 1; break;
    case ACT_FASTEXT:
        if (s && A.shown == A.target && s->links[p.arg] != TTX_NO_LINK)
            goto_page(s->links[p.arg], 1);
        break;
    case ACT_NONE: break;
    }
}

static void go_back(void)
{
    if (A.entry_len) {
        A.entry_len = 0;
        A.top_dirty = A.bottom_dirty = 1;
    } else if (A.history_n) {
        goto_page(A.history[--A.history_n], 0);
    }
}


int app_init(const AppConfig *cfg, const char *config_path)
{
    memset(&A, 0, sizeof(A));
    A.cfg = *cfg;
    A.config_path = config_path;
    A.touch_button = -1;
    A.second = -1;
    if (fetcher_start(cfg->region, cfg->verify_tls))
        return -1;
    goto_page(cfg->page, 0);
    return 0;
}

void app_exit(void)
{
    app_config_save(A.config_path, &A.cfg);
    fetcher_stop();
    ttx_free(&A.page);
}

int app_update(const AppInput *in, uint64_t now_ms, const struct tm *local)
{
    A.now = now_ms;
    A.tm = *local;
    uint32_t k = in->down;

    if (k & IN_START)
        return 0;
    if (k & IN_UP) step_page(1);
    if (k & IN_DOWN) step_page(-1);
    if (k & IN_R) step_page(1);
    if (k & IN_L) {
        A.bottom_off = !A.bottom_off;
        A.bottom_dirty = 1;
    }
    if (k & IN_LEFT) step_sub(-1);
    if (k & IN_RIGHT) step_sub(1);
    if (k & IN_A) do_action((Press){ACT_REVEAL, 0});
    if (k & IN_X) do_action((Press){ACT_HOLD, 0});
    if (k & IN_Y) goto_page(0x100, 1);
    if (k & IN_B) go_back();
    if (k & IN_SELECT) change_region(1);

    // A touch on a dark screen only wakes it.
    if (in->touching && A.touch_button == -1 && A.bottom_off) {
        A.bottom_off = 0;
        A.touch_button = -2;
        A.bottom_dirty = 1;
    } else if (in->touching && A.touch_button == -1) {
        int b = remote_hit(in->tx, in->ty);
        A.touch_button = b >= 0 ? b : -2; // -2: touching empty space
        if (b >= 0) {
            do_action(remote_press(b));
            A.bottom_dirty = 1;
        }
    } else if (!in->touching && A.touch_button != -1) {
        A.touch_button = -1;
        A.bottom_dirty = 1;
    }

    poll_page();

    // Carousel
    const TtxSubpage *s = current_sub();
    if (s && !A.hold && A.page.count > 1 && A.shown == A.target &&
        A.now - A.sub_since >= (uint64_t)s->cycle_secs * 1000) {
        step_sub(1);
    }

    // Header page counter rolls while searching
    if (A.shown != A.target && A.now - A.roll_at >= ROLL_MS) {
        int next = fetcher_step(A.roll, 1);
        A.roll = next > 0 ? next : (A.roll >= 0x8FF ? 0x100 : A.roll + 1);
        A.roll_at = A.now;
        A.top_dirty = 1;
    }

    // flash: on for 3/4 of each second
    int flash_on = (A.now % 1000) < 750;
    if (flash_on != A.flash_on || A.tm.tm_sec != A.second) {
        A.flash_on = flash_on;
        A.second = A.tm.tm_sec;
        A.top_dirty = 1;
    }

    char status[64];
    Color color;
    int online;
    PageState st = fetcher_state(A.target);
    if (A.shown != A.target && st == PAGE_MISSING) {
        snprintf(status, sizeof(status), "P%03X is not in service", A.target);
        color = 0xFF0000;
    } else if (A.shown != A.target) {
        snprintf(status, sizeof(status), "Searching for P%03X...", A.target);
        color = 0xFFFF00;
    } else {
        fetcher_status(status, sizeof(status), &online);
        color = online ? 0x00FF00 : 0xFF0000;
    }
    if (strcmp(status, A.status) || color != A.status_color) {
        memcpy(A.status, status, sizeof(status));
        A.status_color = color;
        A.bottom_dirty = 1;
    }
    return 1;
}


static void put_text(uint8_t *row, int col, const char *text)
{
    for (; *text && col < TTX_COLS; text++)
        row[col++] = (uint8_t)*text;
}

static void build_header(uint8_t *row)
{
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char label[8];
    if (A.entry_len)
        snprintf(label, sizeof(label), "P%s%.*s", A.entry, 3 - A.entry_len, "---");
    else
        snprintf(label, sizeof(label), "P%03X", A.target);

    char rest[40];
    int shown = A.shown != A.target ? A.roll : A.target;
    snprintf(rest, sizeof(rest), "CEEFAX 1 %03X %s %02d %s", shown & 0xFFF, days[A.tm.tm_wday % 7],
             A.tm.tm_mday % 100, months[A.tm.tm_mon % 12]);
    char clock[16];
    snprintf(clock, sizeof(clock), "%02d:%02d/%02d", A.tm.tm_hour % 100, A.tm.tm_min % 100,
             A.tm.tm_sec % 100);

    memset(row, ' ', TTX_COLS);
    // green while searching
    row[0] = A.shown != A.target ? 0x02 : 0x07;
    put_text(row, 1, label);
    put_text(row, 8, rest);
    row[31] = 0x03; // yellow clock
    put_text(row, 32, clock);
}

static void blank_page_message(uint8_t rows[TTX_ROWS][TTX_COLS])
{
    char line[48];
    snprintf(line, sizeof(line), "\x0d\x03 CEEFAX %s", fetch_regions[A.cfg.region].name);
    put_text(rows[10], 1, line);
    if (fetcher_state(A.target) == PAGE_MISSING)
        snprintf(line, sizeof(line), "\x01Page %03X not in service", A.target);
    else
        snprintf(line, sizeof(line), "\x06Searching for page %03X", A.target);
    put_text(rows[13], 2, line);
    put_text(rows[15], 2, "\x07Use the touch screen remote or");
    put_text(rows[16], 2, "\x07the D-Pad to change page.");
}

int app_bottom_on(void)
{
    return !A.bottom_off;
}

// One eye: non-black pixels shifted by dx, black stays at screen depth.
static void draw_eye(const Surface *top, const Surface *page, int dx)
{
    surf_fill(top, 0, 0, top->w, top->h, 0x000000);
    surf_blit_nonblack(top, (top->w - PAGE_W) / 2 + dx, 0, page);
}

int app_draw(const Surface *top_left, const Surface *top_right, const Surface *bottom, int depth)
{
    int drawn = 0;
    if (!top_right)
        depth = 0;
    if (depth != A.depth) {
        A.depth = depth;
        A.top_dirty = 1;
    }
    if (A.top_dirty) {
        uint8_t rows[TTX_ROWS][TTX_COLS];
        const TtxSubpage *s = current_sub();
        if (s) {
            memcpy(rows, s->rows, sizeof(rows));
        } else {
            memset(rows, ' ', sizeof(rows));
            blank_page_message(rows);
        }
        build_header(rows[0]);
        static uint8_t page_px[PAGE_W * 240 * 3];
        Surface page = {page_px, PAGE_W, top_left->h, 3, PAGE_W * 3, 0};
        ttx_render(&page, (const uint8_t(*)[TTX_COLS])rows, A.reveal, A.flash_on);
        draw_eye(top_left, &page, depth);
        if (depth)
            draw_eye(top_right, &page, -depth);
        A.top_dirty = 0;
        drawn |= 1;
    }
    if (A.bottom_dirty) {
        char label[8], info[40];
        if (A.entry_len)
            snprintf(label, sizeof(label), "P%s%.*s", A.entry, 3 - A.entry_len, "---");
        else
            snprintf(label, sizeof(label), "P%03X", A.target);
        if (A.page.count > 1 && A.shown == A.target)
            snprintf(info, sizeof(info), "Subpage %d/%d%s", A.sub + 1, A.page.count, A.hold ? "  HOLD" : "");
        else
            snprintf(info, sizeof(info), "%s", A.hold ? "HOLD" : "");

        RemoteView v = {
            .page_label = label,
            .region = fetch_regions[A.cfg.region].name,
            .info = info,
            .status = A.status,
            .status_color = A.status_color,
            .hold = A.hold,
            .reveal = A.reveal,
            .pressed = A.touch_button,
        };
        const TtxSubpage *s = current_sub();
        for (int i = 0; i < 4; i++)
            v.links[i] = (s && A.shown == A.target) ? s->links[i] : TTX_NO_LINK;
        remote_draw(bottom, &v);
        A.bottom_dirty = 0;
        drawn |= 2;
    }
    return drawn;
}
