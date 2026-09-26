#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

__cervus_lock_t __cervus_heap_lock = CERVUS_LOCK_INIT;

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    if (n > ((size_t)-1) / 2) { __cervus_errno = ENOMEM; return NULL; }
    size_t need = __cervus_align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    __cervus_lock(&__cervus_heap_lock);

    __mblock_t *b = __cervus_bin_fit(need);
    if (b) {
        __cervus_bin_remove(b);
    } else {
        b = __cervus_heap_grow(need);
        if (!b || MB_SIZE(b) < need) {
            if (b) __cervus_bin_insert(b);
            __cervus_errno = ENOMEM;
            __cervus_unlock(&__cervus_heap_lock);
            return NULL;
        }
    }
    b->size = MB_SIZE(b);
    __cervus_mb_split(b, need);
    __cervus_unlock(&__cervus_heap_lock);
    return MB_USER(b);
}
