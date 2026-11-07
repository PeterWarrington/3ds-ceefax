#include "ttx.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_CYCLE_SECS 15

static void subpage_init(TtxSubpage *s, int cycle_secs)
{
    memset(s->rows, ' ', sizeof(s->rows));
    for (int i = 0; i < 6; i++)
        s->links[i] = TTX_NO_LINK;
    s->subcode = 0;
    s->cycle_secs = cycle_secs;
}

// As vbit2: ESC+c is control code c & 0x3f; other bytes < 0x20 end the row.
static void decode_row(uint8_t *dst, const char *src, const char *end)
{
    int col = 0;
    for (const char *p = src; p < end && col < TTX_COLS; p++) {
        uint8_t ch = (uint8_t)*p;
        if (ch == 0x1b) {
            if (++p >= end)
                break;
            ch = (uint8_t)*p & 0x3f;
        } else if (ch < 0x20) {
            break;
        } else {
            ch &= 0x7f;
        }
        dst[col++] = ch;
    }
}

// CT,n,T is n seconds; CT,n,C is n transmission cycles (approximated).
static int parse_cycle(const char *p, const char *end)
{
    int n = (int)strtol(p, NULL, 10);
    const char *comma = memchr(p, ',', (size_t)(end - p));
    int timed = comma && comma + 1 < end && comma[1] == 'T';
    int secs = timed ? n : n * 8;
    if (secs < 5)
        secs = 5;
    if (secs > 60)
        secs = 60;
    return secs;
}

int ttx_parse(const char *data, size_t len, TtxPage *out)
{
    memset(out, 0, sizeof(*out));
    int cap = 0;
    int cycle = DEFAULT_CYCLE_SECS;
    TtxSubpage *cur = NULL;

    const char *end = data + len;
    for (const char *line = data; line < end;) {
        const char *eol = memchr(line, '\n', (size_t)(end - line));
        if (!eol)
            eol = end;
        const char *next = eol < end ? eol + 1 : end;
        if (eol > line && eol[-1] == '\r')
            eol--;

        if (eol - line >= 3 && line[2] == ',') {
            const char *arg = line + 3;
            if (!memcmp(line, "PN", 2)) {
                if (out->count == cap) {
                    cap = cap ? cap * 2 : 4;
                    TtxSubpage *grown = realloc(out->subs, (size_t)cap * sizeof(TtxSubpage));
                    if (!grown) {
                        ttx_free(out);
                        return -1;
                    }
                    out->subs = grown;
                }
                cur = &out->subs[out->count++];
                subpage_init(cur, cycle);
                if (!out->number) {
                    // PN,mppss: keep mpp
                    long pn = strtol(arg, NULL, 16);
                    out->number = (eol - arg >= 5) ? (int)(pn >> 8) : (int)pn;
                }
            } else if (!memcmp(line, "CT", 2)) {
                cycle = parse_cycle(arg, eol);
                if (cur)
                    cur->cycle_secs = cycle;
            } else if (!memcmp(line, "SC", 2) && cur) {
                cur->subcode = (uint16_t)strtol(arg, NULL, 16);
            } else if (!memcmp(line, "OL", 2) && cur) {
                char *after;
                long row = strtol(arg, &after, 10);
                if (after < eol && *after == ',' && row >= 1 && row < TTX_ROWS)
                    decode_row(cur->rows[row], after + 1, eol);
            } else if (!memcmp(line, "FL", 2) && cur) {
                const char *p = arg;
                for (int i = 0; i < 6 && p < eol; i++) {
                    char *after;
                    long link = strtol(p, &after, 16);
                    cur->links[i] = (link >= 0x100 && link <= 0x8FF) ? (uint16_t)link : TTX_NO_LINK;
                    p = (after < eol && *after == ',') ? after + 1 : eol;
                }
            }
        }
        line = next;
    }

    if (!out->count) {
        ttx_free(out);
        return -1;
    }
    return 0;
}

void ttx_free(TtxPage *page)
{
    free(page->subs);
    memset(page, 0, sizeof(*page));
}

int ttx_copy(TtxPage *dst, const TtxPage *src)
{
    memset(dst, 0, sizeof(*dst));
    if (!src->count)
        return 0;
    dst->subs = malloc((size_t)src->count * sizeof(TtxSubpage));
    if (!dst->subs)
        return -1;
    memcpy(dst->subs, src->subs, (size_t)src->count * sizeof(TtxSubpage));
    dst->count = src->count;
    dst->number = src->number;
    return 0;
}
