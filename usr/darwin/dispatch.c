#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/random.h>
#include "darwin.h"

#define DT_NOW     0ull
#define DT_FOREVER (~0ull)

#define OBJ_SEMAPHORE 1

struct dobjhdr {
    int type;
    long refcnt;
};

struct dsem {
    struct dobjhdr h;
    pthread_mutex_t m;
    pthread_cond_t c;
    long value;
    long waiters;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t d_dispatch_time(uint64_t when, int64_t delta)
{
    if (when == DT_FOREVER)
        return DT_FOREVER;
    uint64_t base = when == DT_NOW ? now_ns() : when;
    if (delta < 0 && (uint64_t)-delta > base)
        return 1;
    uint64_t r = base + (uint64_t)delta;
    return r == DT_FOREVER ? r - 1 : r;
}

static uint64_t d_dispatch_walltime(const struct timespec *ts, int64_t delta)
{
    (void)ts;
    return d_dispatch_time(DT_NOW, delta);
}

DSYM("_dispatch_time", d_dispatch_time);
DSYM("_dispatch_walltime", d_dispatch_walltime);

static struct dsem *d_dispatch_semaphore_create(long value)
{
    if (value < 0)
        return NULL;
    struct dsem *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, &ca);
    pthread_condattr_destroy(&ca);
    s->h.type = OBJ_SEMAPHORE;
    s->h.refcnt = 1;
    s->value = value;
    return s;
}

static long d_dispatch_semaphore_signal(struct dsem *s)
{
    pthread_mutex_lock(&s->m);
    s->value++;
    long woke = s->waiters > 0;
    if (woke)
        pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return woke;
}

static long d_dispatch_semaphore_wait(struct dsem *s, uint64_t timeout)
{
    long r = 0;
    pthread_mutex_lock(&s->m);
    s->waiters++;
    while (s->value <= 0) {
        if (timeout == DT_NOW) {
            r = 49;
            break;
        }
        if (timeout == DT_FOREVER) {
            pthread_cond_wait(&s->c, &s->m);
            continue;
        }
        struct timespec ts = { (time_t)(timeout / 1000000000ull), (long)(timeout % 1000000000ull) };
        if (pthread_cond_timedwait(&s->c, &s->m, &ts) == ETIMEDOUT && s->value <= 0) {
            r = 49;
            break;
        }
    }
    s->waiters--;
    if (!r)
        s->value--;
    pthread_mutex_unlock(&s->m);
    return r;
}

DSYM("_dispatch_semaphore_create", d_dispatch_semaphore_create);
DSYM("_dispatch_semaphore_signal", d_dispatch_semaphore_signal);
DSYM("_dispatch_semaphore_wait", d_dispatch_semaphore_wait);

static void d_dispatch_retain(struct dobjhdr *o)
{
    if (o)
        __atomic_add_fetch(&o->refcnt, 1, __ATOMIC_RELAXED);
}

static void d_dispatch_release(struct dobjhdr *o)
{
    if (!o || __atomic_sub_fetch(&o->refcnt, 1, __ATOMIC_ACQ_REL))
        return;
    if (o->type == OBJ_SEMAPHORE) {
        struct dsem *s = (struct dsem *)o;
        pthread_mutex_destroy(&s->m);
        pthread_cond_destroy(&s->c);
    }
    free(o);
}

DSYM("_dispatch_retain", d_dispatch_retain);
DSYM("_dispatch_release", d_dispatch_release);

struct dblock {
    void *isa;
    int flags;
    int reserved;
    void (*invoke)(struct dblock *);
};

static pthread_mutex_t g_once_lock = PTHREAD_MUTEX_INITIALIZER;

static void d_dispatch_once_f(long *pred, void *ctx, void (*fn)(void *))
{
    if (__atomic_load_n(pred, __ATOMIC_ACQUIRE) == ~0l)
        return;
    pthread_mutex_lock(&g_once_lock);
    if (*pred != ~0l) {
        fn(ctx);
        __atomic_store_n(pred, ~0l, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_once_lock);
}

static void call_block(void *b)
{
    struct dblock *blk = b;
    blk->invoke(blk);
}

static void d_dispatch_once(long *pred, struct dblock *blk) { d_dispatch_once_f(pred, blk, call_block); }

DSYM("_dispatch_once_f", d_dispatch_once_f);
DSYM("_dispatch_once", d_dispatch_once);

static int d_CCRandomGenerateBytes(void *b, size_t n)
{
    uint8_t *p = b;
    while (n) {
        size_t c = n > 256 ? 256 : n;
        if (getentropy(p, c) < 0)
            return -4302;
        p += c;
        n -= c;
    }
    return 0;
}
DSYM("_CCRandomGenerateBytes", d_CCRandomGenerateBytes);
