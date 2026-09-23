#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <malloc.h>
#include <libcervus.h>

extern __cervus_lock_t __cervus_heap_lock;

void *memalign(size_t align, size_t n)
{
    if (align <= MB_ALIGN) return malloc(n);
    if (align & (align - 1)) {
        __cervus_errno = EINVAL;
        return NULL;
    }
    if (n > SIZE_MAX - align - 2 * MB_MIN_TOTAL) {
        __cervus_errno = ENOMEM;
        return NULL;
    }
    void *p = malloc(n + align + MB_MIN_TOTAL);
    if (!p) return NULL;

    uintptr_t u = (uintptr_t)p;
    uintptr_t aligned = (u + align - 1) & ~(uintptr_t)(align - 1);
    if (aligned == u) return p;
    while (aligned - u < MB_MIN_TOTAL) aligned += align;

    __cervus_lock(&__cervus_heap_lock);
    __mblock_t *b = MB_FROM_USER(p);
    __mblock_t *nb = MB_FROM_USER((void *)aligned);
    size_t lead = aligned - u;
    size_t total = MB_SIZE(b);
    b->size = lead;
    nb->size = total - lead;
    nb->prev_size = lead;
    __mblock_t *after = __cervus_mb_next(nb);
    if (after) after->prev_size = MB_SIZE(nb);
    __cervus_unlock(&__cervus_heap_lock);

    free(p);
    return (void *)aligned;
}

int posix_memalign(void **out, size_t align, size_t n)
{
    if (!out || align < sizeof(void *) || (align & (align - 1))) return EINVAL;
    int saved = __cervus_errno;
    void *p = memalign(align, n ? n : 1);
    if (!p) {
        int e = __cervus_errno;
        __cervus_errno = saved;
        return e ? e : ENOMEM;
    }
    *out = p;
    return 0;
}

void *aligned_alloc(size_t align, size_t n)
{
    if (!align || (align & (align - 1))) {
        __cervus_errno = EINVAL;
        return NULL;
    }
    return memalign(align, n);
}

void *valloc(size_t n)
{
    return memalign(4096, n);
}

void *pvalloc(size_t n)
{
    return memalign(4096, (n + 4095) & ~(size_t)4095);
}

size_t malloc_usable_size(void *p)
{
    if (!p) return 0;
    return MB_SIZE(MB_FROM_USER(p)) - MB_HDR_SZ;
}
