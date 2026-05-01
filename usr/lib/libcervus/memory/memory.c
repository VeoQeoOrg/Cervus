#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

extern int __cervus_errno;

typedef struct __mblock {
    size_t size;
    size_t prev_size;
} __mblock_t;

#define MB_HDR_SZ        (sizeof(__mblock_t))
#define MB_ALIGN         16
#define MB_MIN_TOTAL     32
#define MB_FREE_BIT      ((size_t)1)
#define MB_SIZE(b)       ((b)->size & ~MB_FREE_BIT)
#define MB_IS_FREE(b)    (((b)->size & MB_FREE_BIT) != 0)
#define MB_USER(b)       ((void *)((char *)(b) + MB_HDR_SZ))
#define MB_FROM_USER(p)  ((__mblock_t *)((char *)(p) - MB_HDR_SZ))

static __mblock_t *__heap_start = NULL;
static __mblock_t *__heap_end   = NULL;

static inline size_t __align_up(size_t n, size_t a) {
    return (n + a - 1) & ~(a - 1);
}

static inline __mblock_t *__mb_next(__mblock_t *b) {
    return (__mblock_t *)((char *)b + MB_SIZE(b));
}

static inline __mblock_t *__mb_prev(__mblock_t *b) {
    if (b->prev_size == 0) return NULL;
    return (__mblock_t *)((char *)b - b->prev_size);
}

static __mblock_t *__heap_grow(size_t need)
{
    size_t chunk = __align_up(need + MB_HDR_SZ, 65536);

    if (!__heap_start) {
        void *base = sbrk((intptr_t)chunk);
        if (base == (void *)-1) return NULL;

        uintptr_t addr    = (uintptr_t)base;
        uintptr_t aligned = (addr + MB_ALIGN - 1) & ~(uintptr_t)(MB_ALIGN - 1);
        size_t lost = aligned - addr;
        if (lost >= chunk - MB_MIN_TOTAL - MB_HDR_SZ) {
            __cervus_errno = ENOMEM;
            return NULL;
        }

        __heap_start = (__mblock_t *)aligned;
        size_t usable = chunk - lost;

        __mblock_t *first = __heap_start;
        size_t first_sz = usable - MB_HDR_SZ;
        first->size      = first_sz | MB_FREE_BIT;
        first->prev_size = 0;

        __heap_end = (__mblock_t *)((char *)first + first_sz);
        __heap_end->size      = 0;
        __heap_end->prev_size = first_sz;

        return first;
    }

    void *p = sbrk((intptr_t)chunk);
    if (p == (void *)-1) return NULL;
    if ((uintptr_t)p != (uintptr_t)__heap_end + MB_HDR_SZ) {
        __cervus_errno = ENOMEM;
        return NULL;
    }

    __mblock_t *new_block = __heap_end;
    new_block->size = chunk | MB_FREE_BIT;

    __mblock_t *new_end = (__mblock_t *)((char *)new_block + MB_SIZE(new_block));
    new_end->size      = 0;
    new_end->prev_size = MB_SIZE(new_block);
    __heap_end = new_end;

    __mblock_t *prev = __mb_prev(new_block);
    if (prev && MB_IS_FREE(prev)) {
        size_t merged_sz = MB_SIZE(prev) + MB_SIZE(new_block);
        prev->size = merged_sz | MB_FREE_BIT;
        __heap_end->prev_size = merged_sz;
        return prev;
    }
    return new_block;
}

static void __mb_split(__mblock_t *b, size_t need)
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

    __mblock_t *after = __mb_next(rest);
    if (after) after->prev_size = MB_SIZE(rest);
}

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    size_t need = __align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    for (__mblock_t *b = __heap_start; b && b != __heap_end; b = __mb_next(b)) {
        if (MB_IS_FREE(b) && MB_SIZE(b) >= need) {
            __mb_split(b, need);
            return MB_USER(b);
        }
    }

    __mblock_t *grown = __heap_grow(need);
    if (!grown) return NULL;
    if (MB_SIZE(grown) < need) {
        __cervus_errno = ENOMEM;
        return NULL;
    }
    __mb_split(grown, need);
    return MB_USER(grown);
}

void *calloc(size_t nm, size_t sz)
{
    size_t t = nm * sz;
    if (nm && t / nm != sz) { __cervus_errno = ENOMEM; return NULL; }
    void *p = malloc(t);
    if (p) memset(p, 0, t);
    return p;
}

void free(void *p)
{
    if (!p) return;
    __mblock_t *b = MB_FROM_USER(p);
    b->size = MB_SIZE(b) | MB_FREE_BIT;

    __mblock_t *next = __mb_next(b);
    if (next != __heap_end && MB_IS_FREE(next)) {
        size_t merged = MB_SIZE(b) + MB_SIZE(next);
        b->size = merged | MB_FREE_BIT;
        __mblock_t *after = __mb_next(b);
        if (after) after->prev_size = merged;
    }
    __mblock_t *prev = __mb_prev(b);
    if (prev && MB_IS_FREE(prev)) {
        size_t merged = MB_SIZE(prev) + MB_SIZE(b);
        prev->size = merged | MB_FREE_BIT;
        __mblock_t *after = __mb_next(prev);
        if (after) after->prev_size = merged;
    }
}

void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    if (n == 0) { free(p); return NULL; }

    __mblock_t *b = MB_FROM_USER(p);
    size_t cur_total = MB_SIZE(b);
    size_t cur_user  = cur_total - MB_HDR_SZ;
    size_t need      = __align_up(n + MB_HDR_SZ, MB_ALIGN);
    if (need < MB_MIN_TOTAL) need = MB_MIN_TOTAL;

    if (need <= cur_total) {
        if (cur_total >= need + MB_MIN_TOTAL) {
            b->size = need;
            __mblock_t *rest = (__mblock_t *)((char *)b + need);
            rest->size      = (cur_total - need) | MB_FREE_BIT;
            rest->prev_size = need;
            __mblock_t *after = __mb_next(rest);
            if (after) after->prev_size = MB_SIZE(rest);
            if (after != __heap_end && MB_IS_FREE(after)) {
                size_t merged = MB_SIZE(rest) + MB_SIZE(after);
                rest->size = merged | MB_FREE_BIT;
                __mblock_t *aft2 = __mb_next(rest);
                if (aft2) aft2->prev_size = merged;
            }
        }
        return p;
    }

    __mblock_t *next = __mb_next(b);
    if (next != __heap_end && MB_IS_FREE(next) &&
        cur_total + MB_SIZE(next) >= need)
    {
        size_t combined = cur_total + MB_SIZE(next);
        b->size = combined;
        __mblock_t *after = __mb_next(b);
        if (after) after->prev_size = combined;
        if (combined >= need + MB_MIN_TOTAL) {
            b->size = need;
            __mblock_t *rest = (__mblock_t *)((char *)b + need);
            rest->size      = (combined - need) | MB_FREE_BIT;
            rest->prev_size = need;
            __mblock_t *aft = __mb_next(rest);
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