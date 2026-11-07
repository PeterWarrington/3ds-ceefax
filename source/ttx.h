// Teletext page model and TTI file parser.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define TTX_ROWS 25
#define TTX_COLS 40
#define TTX_NO_LINK 0x8FF

typedef struct {
    uint8_t rows[TTX_ROWS][TTX_COLS]; // 7-bit teletext bytes, row 0 is the header
    uint16_t links[6];                // fastext: red, green, yellow, cyan, (unused), index
    uint16_t subcode;
    int cycle_secs;                   // carousel display time
} TtxSubpage;

typedef struct {
    int number;                       // magazine+page as hex, e.g. 0x100
    int count;
    TtxSubpage *subs;
} TtxPage;

// Parses a TTI file; returns 0 on success.
int ttx_parse(const char *data, size_t len, TtxPage *out);
void ttx_free(TtxPage *page);
int ttx_copy(TtxPage *dst, const TtxPage *src);
