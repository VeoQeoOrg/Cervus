#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (n == 0) { free(p); return NULL; }
    if (n > ((size_t)-1) / 2) { __cervus_errno = ENOMEM; return NULL; }

    __cervus_lock(&__cervus_heap_lock);

    __mblock_t *b = MB_FROM_USER(p);
    size_t cur_total = MB_SIZE(b);
    size_t cur_user  = cur_total - MB_HDR_SZ;
    size_t need      = __cervus_align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    if (need <= cur_total) {
        __cervus_mb_split(b, need);
        __cervus_unlock(&__cervus_heap_lock);
        return p;
    }

    __mblock_t *next = __cervus_mb_next(b);
    if (next != __cervus_heap_end && MB_IS_FREE(next) &&
        cur_total + MB_SIZE(next) >= need)
    {
        __cervus_bin_remove(next);
        size_t combined = cur_total + MB_SIZE(next);
        b->size = combined;
        __cervus_mb_next(b)->prev_size = combined;
        __cervus_mb_split(b, need);
        __cervus_unlock(&__cervus_heap_lock);
        return p;
    }

    __cervus_unlock(&__cervus_heap_lock);

    void *np = malloc(n);
    if (!np) return NULL;
    memcpy(np, p, cur_user < n ? cur_user : n);
    free(p);
    return np;
}
