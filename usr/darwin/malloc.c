#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>
#include "darwin.h"

struct zone {
    void *reserved1, *reserved2;
    size_t (*size)(struct zone *, const void *);
    void *(*malloc)(struct zone *, size_t);
    void *(*calloc)(struct zone *, size_t, size_t);
    void *(*valloc)(struct zone *, size_t);
    void (*free)(struct zone *, void *);
    void *(*realloc)(struct zone *, void *, size_t);
    void (*destroy)(struct zone *);
    const char *zone_name;
    unsigned (*batch_malloc)(struct zone *, size_t, void **, unsigned);
    void (*batch_free)(struct zone *, void **, unsigned);
    void *introspect;
    unsigned version;
    void *(*memalign)(struct zone *, size_t, size_t);
    void (*free_definite_size)(struct zone *, void *, size_t);
    size_t (*pressure_relief)(struct zone *, size_t);
    int (*claimed_address)(struct zone *, void *);
    void (*try_free_default)(struct zone *, void *);
    void *pad[8];
};

struct zstats {
    unsigned blocks_in_use;
    size_t size_in_use, max_size_in_use, size_allocated;
};

static size_t z_size(struct zone *z, const void *p) { (void)z; return p ? malloc_usable_size((void *)p) : 0; }
static void *z_malloc(struct zone *z, size_t n) { (void)z; return malloc(n); }
static void *z_calloc(struct zone *z, size_t a, size_t b) { (void)z; return calloc(a, b); }
static void *z_valloc(struct zone *z, size_t n) { (void)z; return aligned_alloc(4096, (n + 4095) & ~(size_t)4095); }
static void z_free(struct zone *z, void *p) { (void)z; free(p); }
static void *z_realloc(struct zone *z, void *p, size_t n) { (void)z; return realloc(p, n); }
static void z_destroy(struct zone *z) { (void)z; }
static void z_free_definite(struct zone *z, void *p, size_t n) { (void)z; (void)n; free(p); }
static size_t z_relief(struct zone *z, size_t g) { (void)z; (void)g; return 0; }
static int z_claimed(struct zone *z, void *p) { (void)z; (void)p; return 1; }

static void *z_memalign(struct zone *z, size_t a, size_t n)
{
    (void)z;
    void *p = NULL;
    if (a < sizeof(void *))
        a = sizeof(void *);
    return posix_memalign(&p, a, n) ? NULL : p;
}

static unsigned z_batch_malloc(struct zone *z, size_t n, void **out, unsigned want)
{
    unsigned i = 0;
    for (; i < want; i++)
        if (!(out[i] = z->malloc(z, n)))
            break;
    return i;
}

static void z_batch_free(struct zone *z, void **v, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        z->free(z, v[i]);
}

static struct zone g_default = {
    .size = z_size,
    .malloc = z_malloc,
    .calloc = z_calloc,
    .valloc = z_valloc,
    .free = z_free,
    .realloc = z_realloc,
    .destroy = z_destroy,
    .zone_name = "DefaultMallocZone",
    .batch_malloc = z_batch_malloc,
    .batch_free = z_batch_free,
    .version = 10,
    .memalign = z_memalign,
    .free_definite_size = z_free_definite,
    .pressure_relief = z_relief,
    .claimed_address = z_claimed,
};

static struct zone *g_zones[64] = { &g_default };
static unsigned g_nzones = 1;
static pthread_mutex_t g_zlock = PTHREAD_MUTEX_INITIALIZER;

static struct zone *d_malloc_default_zone(void) { return &g_default; }

static struct zone *d_malloc_create_zone(size_t start, unsigned flags)
{
    (void)start;
    (void)flags;
    struct zone *z = calloc(1, sizeof *z);
    if (!z)
        return NULL;
    *z = g_default;
    z->zone_name = NULL;
    pthread_mutex_lock(&g_zlock);
    if (g_nzones < 64)
        g_zones[g_nzones++] = z;
    pthread_mutex_unlock(&g_zlock);
    return z;
}

static void d_malloc_zone_unregister(struct zone *z)
{
    pthread_mutex_lock(&g_zlock);
    for (unsigned i = 0; i < g_nzones; i++)
        if (g_zones[i] == z) {
            memmove(&g_zones[i], &g_zones[i + 1], (g_nzones - i - 1) * sizeof *g_zones);
            g_nzones--;
            break;
        }
    pthread_mutex_unlock(&g_zlock);
}

static void d_malloc_zone_register(struct zone *z)
{
    d_malloc_zone_unregister(z);
    pthread_mutex_lock(&g_zlock);
    if (g_nzones < 64)
        g_zones[g_nzones++] = z;
    pthread_mutex_unlock(&g_zlock);
}

static void d_malloc_destroy_zone(struct zone *z)
{
    if (z == &g_default)
        return;
    d_malloc_zone_unregister(z);
    if (z->destroy && z->destroy != z_destroy)
        z->destroy(z);
    else
        free(z);
}

static int d_malloc_get_all_zones(unsigned task, void *reader, uintptr_t **out, unsigned *count)
{
    (void)task;
    (void)reader;
    static uintptr_t addrs[64];
    pthread_mutex_lock(&g_zlock);
    for (unsigned i = 0; i < g_nzones; i++)
        addrs[i] = (uintptr_t)g_zones[i];
    *count = g_nzones;
    pthread_mutex_unlock(&g_zlock);
    *out = addrs;
    return 0;
}

static struct zone *d_malloc_zone_from_ptr(const void *p)
{
    if (!p)
        return NULL;
    for (unsigned i = g_nzones; i-- > 1;) {
        struct zone *z = g_zones[i];
        if (z->size != z_size && z->size(z, p))
            return z;
    }
    return &g_default;
}

static void *d_malloc_zone_malloc(struct zone *z, size_t n) { return (z ? z : &g_default)->malloc(z ? z : &g_default, n); }
static void *d_malloc_zone_calloc(struct zone *z, size_t a, size_t b) { return (z ? z : &g_default)->calloc(z ? z : &g_default, a, b); }
static void *d_malloc_zone_valloc(struct zone *z, size_t n) { return (z ? z : &g_default)->valloc(z ? z : &g_default, n); }
static void *d_malloc_zone_realloc(struct zone *z, void *p, size_t n) { return (z ? z : &g_default)->realloc(z ? z : &g_default, p, n); }

static void d_malloc_zone_free(struct zone *z, void *p)
{
    if (!z)
        z = &g_default;
    z->free(z, p);
}

static void *d_malloc_zone_memalign(struct zone *z, size_t a, size_t n)
{
    if (!z)
        z = &g_default;
    return z->version >= 5 && z->memalign ? z->memalign(z, a, n) : z_memalign(z, a, n);
}

static void d_malloc_set_zone_name(struct zone *z, const char *name)
{
    if (z && z != &g_default)
        z->zone_name = name ? strdup(name) : NULL;
}

static const char *d_malloc_get_zone_name(struct zone *z) { return z ? z->zone_name : NULL; }

static void d_malloc_zone_statistics(struct zone *z, struct zstats *st)
{
    (void)z;
    memset(st, 0, sizeof *st);
}

static size_t d_malloc_zone_pressure_relief(struct zone *z, size_t goal) { (void)z; (void)goal; return 0; }
static int d_malloc_zone_check(struct zone *z) { (void)z; return 1; }
static void d_malloc_zone_print(struct zone *z, int v) { (void)z; (void)v; }
static void d_malloc_noop(void *p) { (void)p; }
static int d_malloc_make_nonpurgeable(void *p) { (void)p; return 0; }

DSYM("_malloc_default_zone", d_malloc_default_zone);
DSYM("_malloc_default_purgeable_zone", d_malloc_default_zone);
DSYM("_malloc_create_zone", d_malloc_create_zone);
DSYM("_malloc_destroy_zone", d_malloc_destroy_zone);
DSYM("_malloc_zone_register", d_malloc_zone_register);
DSYM("_malloc_zone_unregister", d_malloc_zone_unregister);
DSYM("_malloc_get_all_zones", d_malloc_get_all_zones);
DSYM("_malloc_zone_from_ptr", d_malloc_zone_from_ptr);
DSYM("_malloc_zone_malloc", d_malloc_zone_malloc);
DSYM("_malloc_zone_calloc", d_malloc_zone_calloc);
DSYM("_malloc_zone_valloc", d_malloc_zone_valloc);
DSYM("_malloc_zone_realloc", d_malloc_zone_realloc);
DSYM("_malloc_zone_free", d_malloc_zone_free);
DSYM("_malloc_zone_memalign", d_malloc_zone_memalign);
DSYM("_malloc_set_zone_name", d_malloc_set_zone_name);
DSYM("_malloc_get_zone_name", d_malloc_get_zone_name);
DSYM("_malloc_zone_statistics", d_malloc_zone_statistics);
DSYM("_malloc_zone_pressure_relief", d_malloc_zone_pressure_relief);
DSYM("_malloc_zone_check", d_malloc_zone_check);
DSYM("_malloc_zone_print", d_malloc_zone_print);
DSYM("_malloc_make_purgeable", d_malloc_noop);
DSYM("_malloc_make_nonpurgeable", d_malloc_make_nonpurgeable);
