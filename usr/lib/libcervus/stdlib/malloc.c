#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>
#include <libcervus.h>

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    size_t need = __cervus_align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    for (__mblock_t *b = __cervus_heap_start; b && b != __cervus_heap_end; b = __cervus_mb_next(b)) {
        if (MB_IS_FREE(b) && MB_SIZE(b) >= need) {
            __cervus_mb_split(b, need);
            return MB_USER(b);
        }
    }

    __mblock_t *grown = __cervus_heap_grow(need);
    if (!grown) return NULL;
    if (MB_SIZE(grown) < need) {
        __cervus_errno = ENOMEM;
        return NULL;
    }
    __cervus_mb_split(grown, need);
    return MB_USER(grown);
}
