#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

__cervus_lock_t __cervus_heap_lock = CERVUS_LOCK_INIT;

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    size_t need = __cervus_align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    __cervus_lock(&__cervus_heap_lock);

    for (__mblock_t *b = __cervus_heap_start; b && b != __cervus_heap_end; b = __cervus_mb_next(b)) {
        if (MB_IS_FREE(b) && MB_SIZE(b) >= need) {
            __cervus_mb_split(b, need);
            __cervus_unlock(&__cervus_heap_lock);
            return MB_USER(b);
        }
    }

    __mblock_t *grown = __cervus_heap_grow(need);
    if (!grown) { __cervus_unlock(&__cervus_heap_lock); return NULL; }
    if (MB_SIZE(grown) < need) {
        __cervus_errno = ENOMEM;
        __cervus_unlock(&__cervus_heap_lock);
        return NULL;
    }
    __cervus_mb_split(grown, need);
    __cervus_unlock(&__cervus_heap_lock);
    return MB_USER(grown);
}
