#include <stdlib.h>
#include <stddef.h>
#include <libcervus.h>

void free(void *p)
{
    if (!p) return;
    __mblock_t *b = MB_FROM_USER(p);
    b->size = MB_SIZE(b) | MB_FREE_BIT;

    __mblock_t *next = __cervus_mb_next(b);
    if (next != __cervus_heap_end && MB_IS_FREE(next)) {
        size_t merged = MB_SIZE(b) + MB_SIZE(next);
        b->size = merged | MB_FREE_BIT;
        __mblock_t *after = __cervus_mb_next(b);
        if (after) after->prev_size = merged;
    }
    __mblock_t *prev = __cervus_mb_prev(b);
    if (prev && MB_IS_FREE(prev)) {
        size_t merged = MB_SIZE(prev) + MB_SIZE(b);
        prev->size = merged | MB_FREE_BIT;
        __mblock_t *after = __cervus_mb_next(prev);
        if (after) after->prev_size = merged;
    }
}
