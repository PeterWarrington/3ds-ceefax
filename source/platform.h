// Threading/time shim so the core also builds on desktop.
#pragma once
#include <stdint.h>

#ifdef __3DS__
#include <3ds.h>

typedef LightLock plat_mutex;
typedef Thread plat_thread;

static inline void plat_lock_init(plat_mutex *l) { LightLock_Init(l); }
static inline void plat_lock(plat_mutex *l) { LightLock_Lock(l); }
static inline void plat_unlock(plat_mutex *l) { LightLock_Unlock(l); }
static inline void plat_sleep_ms(int ms) { svcSleepThread((int64_t)ms * 1000000LL); }
static inline uint64_t plat_now_ms(void) { return osGetTime(); }

static inline int plat_thread_start(plat_thread *t, void (*fn)(void *), void *arg)
{
    // Must stay below libctru's APT thread (0x31).
    *t = threadCreate(fn, arg, 256 * 1024, 0x3F, -2, false);
    return *t ? 0 : -1;
}

// Returns 0 if the thread finished within 3s.
static inline int plat_thread_join(plat_thread *t)
{
    if (R_FAILED(threadJoin(*t, 3000000000ULL)))
        return -1;
    threadFree(*t);
    return 0;
}

#define PLAT_CA_BUNDLE "romfs:/cacert.pem"

#else
#include <pthread.h>
#include <time.h>

typedef pthread_mutex_t plat_mutex;
typedef pthread_t plat_thread;

static inline void plat_lock_init(plat_mutex *l) { pthread_mutex_init(l, NULL); }
static inline void plat_lock(plat_mutex *l) { pthread_mutex_lock(l); }
static inline void plat_unlock(plat_mutex *l) { pthread_mutex_unlock(l); }
static inline void plat_sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
static inline uint64_t plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

typedef struct {
    void (*fn)(void *);
    void *arg;
} plat_thunk;

static inline void *plat_trampoline(void *p)
{
    plat_thunk t = *(plat_thunk *)p;
    t.fn(t.arg);
    return NULL;
}

static inline int plat_thread_start(plat_thread *t, void (*fn)(void *), void *arg)
{
    static plat_thunk thunk;
    thunk.fn = fn;
    thunk.arg = arg;
    return pthread_create(t, NULL, plat_trampoline, &thunk);
}

static inline int plat_thread_join(plat_thread *t) { return pthread_join(*t, NULL); }

#define PLAT_CA_BUNDLE NULL // system store

#endif
