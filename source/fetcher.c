#include "fetcher.h"

#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "platform.h"

const Region fetch_regions[] = {
    {"London", "London"},
    {"East", "East"},
    {"EastMidlands", "East Midlands"},
    {"NorthernIreland", "N. Ireland"},
    {"Scotland", "Scotland"},
    {"South", "South"},
    {"SouthWest", "South West"},
    {"Wales", "Wales"},
    {"West", "West"},
    {"Worldwide", "Worldwide"},
    {"Yorks%26Lincs", "Yorks & Lincs"},
};
const int fetch_region_count = sizeof(fetch_regions) / sizeof(fetch_regions[0]);

#define NPAGES 0x800 // pages 0x100-0x8FF
#define MAX_PREFETCH 16

typedef struct {
    TtxPage page;
    char etag[96];
    uint32_t version;
    uint8_t stale;   // needs revalidating
    uint8_t missing; // server said 404
    uint8_t loaded;  // has ever been fetched
} Entry;

static struct {
    plat_mutex lock;
    plat_thread thread;
    volatile int running;
    int verify_tls;

    int region, region_gen;
    int want;
    int prefetch[MAX_PREFETCH];
    int prefetch_n;

    Entry *cache[NPAGES];
    uint8_t exists[NPAGES];
    int have_index;
    long index_rev;
    uint64_t last_index_ms;

    char status[64];
    int online;
} F;

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

static int page_ok(int page)
{
    return page >= 0x100 && page <= 0x8FF;
}

static void set_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    plat_lock(&F.lock);
    vsnprintf(F.status, sizeof(F.status), fmt, ap);
    plat_unlock(&F.lock);
    va_end(ap);
}


static size_t on_body(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    Buf *b = userdata;
    size_t n = size * nmemb;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 16384;
        while (cap < b->len + n + 1)
            cap *= 2;
        char *grown = realloc(b->data, cap);
        if (!grown)
            return 0;
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = 0;
    return n;
}

static size_t on_header(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    char *etag = userdata;
    size_t n = size * nmemb;
    if (n > 5 && !strncasecmp(ptr, "etag:", 5)) {
        const char *v = ptr + 5;
        const char *end = ptr + n;
        while (v < end && *v == ' ')
            v++;
        while (end > v && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' '))
            end--;
        size_t len = (size_t)(end - v);
        if (len < 96) {
            memcpy(etag, v, len);
            etag[len] = 0;
        }
    }
    return n;
}

// Returns the HTTP status, or -1 on transport failure.
static long http_get(CURL *h, const char *url, const char *etag, Buf *body, char *etag_out)
{
    body->len = 0;
    if (etag_out)
        etag_out[0] = 0;
    struct curl_slist *hdrs = NULL;
    char inm[128];
    if (etag && etag[0]) {
        snprintf(inm, sizeof(inm), "If-None-Match: %s", etag);
        hdrs = curl_slist_append(hdrs, inm);
    }

    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, etag_out ? on_header : NULL);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, etag_out);

    CURLcode rc = curl_easy_perform(h);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, NULL);
    curl_slist_free_all(hdrs);
    if (rc != CURLE_OK) {
        set_status("Net error: %s", curl_easy_strerror(rc));
        return -1;
    }
    long code = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    return code;
}

// Abort transfers promptly when the app is closing.
static int on_progress(void *p, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
    (void)p, (void)dt, (void)dn, (void)ut, (void)un;
    return !F.running;
}

static CURL *http_open(void)
{
    CURL *h = curl_easy_init();
    if (!h)
        return NULL;
    curl_easy_setopt(h, CURLOPT_USERAGENT, "3ds-ceefax/1.0");
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, on_progress);
    if (PLAT_CA_BUNDLE)
        curl_easy_setopt(h, CURLOPT_CAINFO, PLAT_CA_BUNDLE);
    if (!F.verify_tls) {
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    return h;
}

// Cache helpers: call with the lock held.

static Entry *entry(int page, int create)
{
    Entry **slot = &F.cache[page - 0x100];
    if (!*slot && create)
        *slot = calloc(1, sizeof(Entry));
    return *slot;
}

static void clear_cache(void)
{
    for (int i = 0; i < NPAGES; i++) {
        if (F.cache[i]) {
            ttx_free(&F.cache[i]->page);
            free(F.cache[i]);
            F.cache[i] = NULL;
        }
    }
    memset(F.exists, 0, sizeof(F.exists));
    F.have_index = 0;
    F.index_rev = -1;
    F.last_index_ms = 0;
}

static int needs_fetch(int page)
{
    if (!page_ok(page))
        return 0;
    if (F.have_index && !F.exists[page - 0x100])
        return 0;
    Entry *e = F.cache[page - 0x100];
    return !e || !e->loaded || e->stale;
}


static void fetch_page(CURL *h, int page, int gen, Buf *body)
{
    char url[160], etag_in[96] = "", etag_out[96];
    plat_lock(&F.lock);
    snprintf(url, sizeof(url), FETCH_BASE_URL "%s/P%03X.tti", fetch_regions[F.region].dir, page);
    Entry *e = entry(page, 0);
    if (e && e->loaded)
        strcpy(etag_in, e->etag);
    plat_unlock(&F.lock);

    long code = http_get(h, url, etag_in, body, etag_out);

    TtxPage parsed = {0};
    int ok = code == 200 && ttx_parse(body->data, body->len, &parsed) == 0;

    plat_lock(&F.lock);
    if (gen != F.region_gen) {
        // region changed mid-flight
        plat_unlock(&F.lock);
        ttx_free(&parsed);
        return;
    }
    e = entry(page, 1);
    if (!e) {
        plat_unlock(&F.lock);
        ttx_free(&parsed);
        return;
    }
    if (ok) {
        ttx_free(&e->page);
        e->page = parsed;
        if (!parsed.number)
            e->page.number = page;
        snprintf(e->etag, sizeof(e->etag), "%s", etag_out);
        e->version++;
        e->loaded = 1;
        e->stale = 0;
        e->missing = 0;
        F.online = 1;
    } else if (code == 304) {
        e->stale = 0;
        F.online = 1;
    } else if (code == 404 || code == 200) {
        e->missing = 1;
        e->loaded = 1;
        e->stale = 0;
        F.online = 1;
    } else {
        F.online = 0;
        if (code > 0)
            snprintf(F.status, sizeof(F.status), "HTTP %ld for P%03X", code, page);
    }
    plat_unlock(&F.lock);
}

// Subversion XML index: <index rev="N"> and <file name="P100.tti"/> entries.
static void fetch_index(CURL *h, int gen, Buf *body)
{
    char url[160];
    plat_lock(&F.lock);
    snprintf(url, sizeof(url), FETCH_BASE_URL "%s/", fetch_regions[F.region].dir);
    plat_unlock(&F.lock);

    long code = http_get(h, url, NULL, body, NULL);
    uint64_t now = plat_now_ms();

    plat_lock(&F.lock);
    F.last_index_ms = now;
    if (gen != F.region_gen) {
        F.last_index_ms = 0;
        plat_unlock(&F.lock);
        return;
    }
    if (code != 200) {
        F.online = 0;
        if (code > 0)
            snprintf(F.status, sizeof(F.status), "HTTP %ld fetching index", code);
        plat_unlock(&F.lock);
        return;
    }
    plat_unlock(&F.lock);
    if (!body->data)
        return;

    static uint8_t found[NPAGES];
    memset(found, 0, sizeof(found));
    long rev = -1;
    const char *r = strstr(body->data, "<index rev=\"");
    if (r)
        rev = strtol(r + 12, NULL, 10);
    for (const char *p = body->data; (p = strstr(p, "name=\"P")) != NULL; p += 7) {
        char *end;
        long page = strtol(p + 7, &end, 16);
        if (end == p + 10 && !strncmp(end, ".tti\"", 5) && page_ok((int)page))
            found[page - 0x100] = 1;
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    plat_lock(&F.lock);
    if (gen == F.region_gen) {
        if (rev != F.index_rev) {
            // revalidate lazily
            for (int i = 0; i < NPAGES; i++)
                if (F.cache[i])
                    F.cache[i]->stale = 1;
            F.index_rev = rev;
        }
        memcpy(F.exists, found, sizeof(found));
        F.have_index = 1;
        F.online = 1;
        snprintf(F.status, sizeof(F.status), "Checked %02d:%02d:%02d  rev %ld", tm->tm_hour, tm->tm_min,
                 tm->tm_sec, rev);
    }
    plat_unlock(&F.lock);
}

static void worker(void *arg)
{
    (void)arg;
    CURL *h = http_open();
    Buf body = {0};
    int failures = 0;

    while (F.running) {
        plat_lock(&F.lock);
        int gen = F.region_gen;
        int job_page = -1, job_index = 0;
        uint64_t now = plat_now_ms();

        if (needs_fetch(F.want)) {
            job_page = F.want;
        } else if (!F.have_index || now - F.last_index_ms >= FETCH_POLL_MS) {
            job_index = 1;
        } else {
            while (F.prefetch_n > 0 && job_page < 0) {
                int p = F.prefetch[--F.prefetch_n];
                if (needs_fetch(p))
                    job_page = p;
            }
        }
        // back off while offline
        int backoff = !F.online && failures > 0 && (job_page >= 0 || job_index);
        plat_unlock(&F.lock);

        if (backoff)
            for (int i = 0; i < (failures < 5 ? failures : 5) * 10 && F.running; i++)
                plat_sleep_ms(100);

        if (job_page >= 0) {
            fetch_page(h, job_page, gen, &body);
        } else if (job_index) {
            fetch_index(h, gen, &body);
        } else {
            plat_sleep_ms(100);
            continue;
        }
        plat_lock(&F.lock);
        failures = F.online ? 0 : failures + 1;
        plat_unlock(&F.lock);
    }

    free(body.data);
    curl_easy_cleanup(h);
}


int fetcher_start(int region, int verify_tls)
{
    memset(&F, 0, sizeof(F));
    plat_lock_init(&F.lock);
    F.region = (region >= 0 && region < fetch_region_count) ? region : 0;
    F.verify_tls = verify_tls;
    F.index_rev = -1;
    F.want = -1;
    F.online = 1;
    snprintf(F.status, sizeof(F.status), "Connecting...");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    F.running = 1;
    if (plat_thread_start(&F.thread, worker, NULL)) {
        F.running = 0;
        return -1;
    }
    return 0;
}

void fetcher_stop(void)
{
    if (!F.running)
        return;
    F.running = 0;
    if (plat_thread_join(&F.thread))
        return; // still running; leave its memory alone
    clear_cache();
    curl_global_cleanup();
}

void fetcher_set_region(int region)
{
    plat_lock(&F.lock);
    if (region >= 0 && region < fetch_region_count && region != F.region) {
        F.region = region;
        F.region_gen++;
        clear_cache();
        F.prefetch_n = 0;
        F.online = 1;
        snprintf(F.status, sizeof(F.status), "Tuning to %s...", fetch_regions[region].name);
    }
    plat_unlock(&F.lock);
}

void fetcher_want(int page)
{
    plat_lock(&F.lock);
    F.want = page;
    plat_unlock(&F.lock);
}

void fetcher_prefetch(const int *pages, int n)
{
    plat_lock(&F.lock);
    F.prefetch_n = 0;
    // stack: push in reverse
    for (int i = n - 1; i >= 0 && F.prefetch_n < MAX_PREFETCH; i--)
        if (page_ok(pages[i]))
            F.prefetch[F.prefetch_n++] = pages[i];
    plat_unlock(&F.lock);
}

int fetcher_get(int page, TtxPage *out, uint32_t *version)
{
    int got = 0;
    if (!page_ok(page))
        return 0;
    plat_lock(&F.lock);
    Entry *e = F.cache[page - 0x100];
    if (e && e->loaded && !e->missing && e->version != *version && ttx_copy(out, &e->page) == 0) {
        *version = e->version;
        got = 1;
    }
    plat_unlock(&F.lock);
    return got;
}

PageState fetcher_state(int page)
{
    if (!page_ok(page))
        return PAGE_MISSING;
    PageState st;
    plat_lock(&F.lock);
    Entry *e = F.cache[page - 0x100];
    if (F.have_index && !F.exists[page - 0x100])
        st = PAGE_MISSING;
    else if (e && e->loaded)
        st = e->missing ? PAGE_MISSING : PAGE_READY;
    else
        st = F.want == page ? PAGE_LOADING : PAGE_UNKNOWN;
    plat_unlock(&F.lock);
    return st;
}

int fetcher_step(int page, int dir)
{
    int result = -1;
    plat_lock(&F.lock);
    if (F.have_index) {
        int i = page_ok(page) ? page - 0x100 : 0;
        for (int n = 0; n < NPAGES; n++) {
            i = (i + dir + NPAGES) % NPAGES;
            if (F.exists[i]) {
                result = i + 0x100;
                break;
            }
        }
    }
    plat_unlock(&F.lock);
    return result;
}

void fetcher_status(char *buf, size_t n, int *online)
{
    plat_lock(&F.lock);
    snprintf(buf, n, "%s", F.status);
    if (online)
        *online = F.online;
    plat_unlock(&F.lock);
}
