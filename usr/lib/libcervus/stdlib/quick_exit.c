#include <stdlib.h>
#include <unistd.h>
#include <libcervus.h>

#define QUICK_EXIT_MAX 32

static void (*g_funcs[QUICK_EXIT_MAX])(void);
static int g_count;
static __cervus_lock_t g_lock = CERVUS_LOCK_INIT;

int at_quick_exit(void (*fn)(void))
{
    int r = -1;
    __cervus_lock(&g_lock);
    if (g_count < QUICK_EXIT_MAX) {
        g_funcs[g_count++] = fn;
        r = 0;
    }
    __cervus_unlock(&g_lock);
    return r;
}

void quick_exit(int status)
{
    __cervus_lock(&g_lock);
    while (g_count > 0) {
        void (*fn)(void) = g_funcs[--g_count];
        __cervus_unlock(&g_lock);
        fn();
        __cervus_lock(&g_lock);
    }
    _Exit(status);
}
