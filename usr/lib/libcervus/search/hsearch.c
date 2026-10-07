#include <search.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

static ENTRY *g_tab;
static size_t g_mask, g_used;

static size_t hash_of(const char *s)
{
    size_t h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

int hcreate(size_t nel)
{
    size_t n = 8;
    while (n < nel + nel / 2 + 1 && n < ((size_t)1 << 30)) n <<= 1;
    ENTRY *t = calloc(n, sizeof *t);
    if (!t) return 0;
    free(g_tab);
    g_tab = t;
    g_mask = n - 1;
    g_used = 0;
    return 1;
}

void hdestroy(void)
{
    free(g_tab);
    g_tab = NULL;
    g_mask = g_used = 0;
}

ENTRY *hsearch(ENTRY item, ACTION action)
{
    if (!g_tab) { __cervus_set_errno(ESRCH); return NULL; }
    for (size_t i = hash_of(item.key) & g_mask, k = 0; k <= g_mask; i = (i + 1) & g_mask, k++) {
        if (!g_tab[i].key) {
            if (action == FIND || g_used + 1 > g_mask) { __cervus_set_errno(action == FIND ? ESRCH : ENOMEM); return NULL; }
            g_tab[i] = item;
            g_used++;
            return &g_tab[i];
        }
        if (!strcmp(g_tab[i].key, item.key)) return &g_tab[i];
    }
    __cervus_set_errno(ENOMEM);
    return NULL;
}
