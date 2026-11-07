// Background page downloader. Each region is a Subversion repository of
// TTI files; its index is polled and pages are revalidated with ETags.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "ttx.h"

#define FETCH_BASE_URL "https://feeds.nmsni.co.uk/svn/ceefax/"
#define FETCH_POLL_MS 30000

typedef enum {
    PAGE_UNKNOWN,   // not requested yet
    PAGE_LOADING,
    PAGE_READY,
    PAGE_MISSING,   // not in the service
} PageState;

typedef struct {
    const char *dir;   // repository directory
    const char *name;  // display name
} Region;

extern const Region fetch_regions[];
extern const int fetch_region_count;

int fetcher_start(int region, int verify_tls);
void fetcher_stop(void);
void fetcher_set_region(int region);

// Fetch this page first.
void fetcher_want(int page);
void fetcher_prefetch(const int *pages, int n);

// Copies the page into `out` and returns 1 if newer than *version.
int fetcher_get(int page, TtxPage *out, uint32_t *version);
PageState fetcher_state(int page);

// Next/previous existing page, or -1 before the index loads.
int fetcher_step(int page, int dir);

void fetcher_status(char *buf, size_t n, int *online);
