#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <libcervus.h>
#include <libcervus.h>

__mblock_t *__cervus_heap_start = NULL;
__mblock_t *__cervus_heap_end   = NULL;

__mblock_t *__cervus_heap_grow(size_t need)
{
    size_t chunk = __cervus_align_up(need + MB_HDR_SZ, 65536);

    if (!__cervus_heap_start) {
        void *base = sbrk((intptr_t)chunk);
        if (base == (void *)-1) return NULL;

        uintptr_t addr    = (uintptr_t)base;
        uintptr_t aligned = (addr + MB_ALIGN - 1) & ~(uintptr_t)(MB_ALIGN - 1);
        size_t lost = aligned - addr;
        if (lost >= chunk - MB_MIN_TOTAL - MB_HDR_SZ) {
            __cervus_errno = ENOMEM;
            return NULL;
        }

        __cervus_heap_start = (__mblock_t *)aligned;
        size_t usable = chunk - lost;

        __mblock_t *first = __cervus_heap_start;
        size_t first_sz = usable - MB_HDR_SZ;
        first->size      = first_sz | MB_FREE_BIT;
        first->prev_size = 0;

        __cervus_heap_end = (__mblock_t *)((char *)first + first_sz);
        __cervus_heap_end->size      = 0;
        __cervus_heap_end->prev_size = first_sz;

        return first;
    }

    void *p = sbrk((intptr_t)chunk);
    if (p == (void *)-1) return NULL;
    if ((uintptr_t)p != (uintptr_t)__cervus_heap_end + MB_HDR_SZ) {
        __cervus_errno = ENOMEM;
        return NULL;
    }

    __mblock_t *new_block = __cervus_heap_end;
    new_block->size = chunk | MB_FREE_BIT;

    __mblock_t *new_end = (__mblock_t *)((char *)new_block + MB_SIZE(new_block));
    new_end->size      = 0;
    new_end->prev_size = MB_SIZE(new_block);
    __cervus_heap_end = new_end;

    __mblock_t *prev = __cervus_mb_prev(new_block);
    if (prev && MB_IS_FREE(prev)) {
        size_t merged_sz = MB_SIZE(prev) + MB_SIZE(new_block);
        prev->size = merged_sz | MB_FREE_BIT;
        __cervus_heap_end->prev_size = merged_sz;
        return prev;
    }
    return new_block;
}

void __cervus_mb_split(__mblock_t *b, size_t need)
{
    size_t cur = MB_SIZE(b);
    if (cur < need + MB_MIN_TOTAL) {
        b->size = cur;
        return;
    }
    b->size = need;

    __mblock_t *rest = (__mblock_t *)((char *)b + need);
    rest->size      = (cur - need) | MB_FREE_BIT;
    rest->prev_size = need;

    __mblock_t *after = __cervus_mb_next(rest);
    if (after) after->prev_size = MB_SIZE(rest);
}
