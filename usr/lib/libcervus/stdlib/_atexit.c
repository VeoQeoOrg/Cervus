#include <stdlib.h>
#include <string.h>
#include <libcervus.h>

typedef struct {
    void (*fn)(void *);
    void  *arg;
    void  *dso;
    int    plain;
    int    done;
} exit_fn_t;

static exit_fn_t  g_static[64];
static exit_fn_t *g_fns = g_static;
static int        g_cnt;
static int        g_cap = 64;

__attribute__((weak)) void *__dso_handle = &__dso_handle;

int __cervus_exit_push(void (*fn)(void *), void *arg, void *dso, int plain)
{
    if (!fn) return -1;
    if (g_cnt == g_cap) {
        int ncap = g_cap * 2;
        exit_fn_t *n = malloc((size_t)ncap * sizeof *n);
        if (!n) return -1;
        memcpy(n, g_fns, (size_t)g_cnt * sizeof *n);
        if (g_fns != g_static) free(g_fns);
        g_fns = n;
        g_cap = ncap;
    }
    g_fns[g_cnt].fn    = fn;
    g_fns[g_cnt].arg   = arg;
    g_fns[g_cnt].dso   = dso;
    g_fns[g_cnt].plain = plain;
    g_fns[g_cnt].done  = 0;
    g_cnt++;
    return 0;
}

void __cervus_run_exit_fns(void *dso)
{
    for (int i = g_cnt - 1; i >= 0; i--) {
        exit_fn_t *e = &g_fns[i];
        if (e->done) continue;
        if (dso && e->dso != dso) continue;
        e->done = 1;
        if (e->plain) ((void (*)(void))e->fn)();
        else          e->fn(e->arg);
    }
    if (!dso) g_cnt = 0;
}

int __cxa_atexit(void (*fn)(void *), void *arg, void *dso)
{
    return __cervus_exit_push(fn, arg, dso, 0);
}

void __cxa_finalize(void *dso)
{
    __cervus_run_exit_fns(dso);
}
