// Fetcher smoke test against the live feed.
#include <stdio.h>

#include "../source/fetcher.h"
#include "../source/platform.h"

static int wait_page(int page, TtxPage *out, uint32_t *ver)
{
    for (int i = 0; i < 300; i++) {
        if (fetcher_get(page, out, ver))
            return 1;
        if (fetcher_state(page) == PAGE_MISSING)
            return 0;
        plat_sleep_ms(100);
    }
    return 0;
}

int main(void)
{
    char status[64];
    TtxPage p = {0};
    uint32_t ver = 0;
    fetcher_start(0, 1);

    fetcher_want(0x100);
    int ok = wait_page(0x100, &p, &ver);
    fetcher_status(status, sizeof(status), NULL);
    printf("London P100: ok=%d subpages=%d ver=%u status='%s'\n", ok, p.count, ver, status);
    ttx_free(&p);

    plat_sleep_ms(1500); // let the index load
    printf("step from 100: next=%03X prev=%03X\n", fetcher_step(0x100, 1), fetcher_step(0x100, -1));
    printf("state of P999-ish 0x8FE: %d (3=missing)\n", fetcher_state(0x8FE));

    int pf[] = {0x101, 0x300};
    fetcher_prefetch(pf, 2);
    plat_sleep_ms(3000);
    uint32_t v2 = 0;
    printf("prefetched 101 ready: %d\n", fetcher_get(0x101, &p, &v2));
    ttx_free(&p);

    fetcher_set_region(7); // Wales
    ver = 0;
    fetcher_want(0x100);
    plat_sleep_ms(200);
    ok = wait_page(0x100, &p, &ver);
    fetcher_status(status, sizeof(status), NULL);
    printf("Wales P100: ok=%d subpages=%d row5='%.40s' status='%s'\n", ok, p.count, p.count ? (char *)p.subs[0].rows[5] : "", status);
    ttx_free(&p);
    fetcher_stop();
    return 0;
}
