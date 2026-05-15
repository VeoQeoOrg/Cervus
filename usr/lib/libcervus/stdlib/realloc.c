#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <libcervus.h>

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (n == 0) { free(p); return NULL; }

    __mblock_t *b = MB_FROM_USER(p);
    size_t cur_total = MB_SIZE(b);
    size_t cur_user  = cur_total - MB_HDR_SZ;
    size_t need      = __cervus_align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    if (need <= cur_total) {
        if (cur_total >= need + MB_MIN_TOTAL) {
            b->size = need;
            __mblock_t *rest = (__mblock_t *)((char *)b + need);
            rest->size      = (cur_total - need) | MB_FREE_BIT;
            rest->prev_size = need;
            __mblock_t *after = __cervus_mb_next(rest);
            if (after) after->prev_size = MB_SIZE(rest);
            if (after != __cervus_heap_end && MB_IS_FREE(after)) {
                size_t merged = MB_SIZE(rest) + MB_SIZE(after);
                rest->size = merged | MB_FREE_BIT;
                __mblock_t *aft2 = __cervus_mb_next(rest);
                if (aft2) aft2->prev_size = merged;
            }
        }
        return p;
    }

    __mblock_t *next = __cervus_mb_next(b);
    if (next != __cervus_heap_end && MB_IS_FREE(next) &&
        cur_total + MB_SIZE(next) >= need)
    {
        size_t combined = cur_total + MB_SIZE(next);
        b->size = combined;
        __mblock_t *after = __cervus_mb_next(b);
        if (after) after->prev_size = combined;
        if (combined >= need + MB_MIN_TOTAL) {
            b->size = need;
            __mblock_t *rest = (__mblock_t *)((char *)b + need);
            rest->size      = (combined - need) | MB_FREE_BIT;
            rest->prev_size = need;
            __mblock_t *aft = __cervus_mb_next(rest);
            if (aft) aft->prev_size = MB_SIZE(rest);
        }
        return p;
    }

    void *np = malloc(n);
    if (!np) return NULL;
    memcpy(np, p, cur_user);
    free(p);
    return np;
}
