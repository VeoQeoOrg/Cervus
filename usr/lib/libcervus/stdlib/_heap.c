#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <libcervus.h>

__mblock_t *__cervus_heap_start = NULL;
__mblock_t *__cervus_heap_end   = NULL;

#define NBINS 128

static __mblock_t *g_bins[NBINS];
static uint64_t    g_binmap[NBINS / 64];

#define FNEXT(b) (((__mblock_t **)MB_USER(b))[0])
#define FPREV(b) (((__mblock_t **)MB_USER(b))[1])

static int bin_of(size_t sz)
{
    if (sz <= 1024) return (int)(sz >> 4) - 2;
    int lg = 63 - __builtin_clzll((unsigned long long)sz);
    int b = 63 + (lg - 10) * 4 + (int)((sz >> (lg - 2)) & 3);
    return b < NBINS ? b : NBINS - 1;
}

void __cervus_bin_insert(__mblock_t *b)
{
    int i = bin_of(MB_SIZE(b));
    FPREV(b) = NULL;
    FNEXT(b) = g_bins[i];
    if (g_bins[i]) FPREV(g_bins[i]) = b;
    g_bins[i] = b;
    g_binmap[i >> 6] |= 1ULL << (i & 63);
}

void __cervus_bin_remove(__mblock_t *b)
{
    int i = bin_of(MB_SIZE(b));
    if (FPREV(b)) FNEXT(FPREV(b)) = FNEXT(b);
    else          g_bins[i] = FNEXT(b);
    if (FNEXT(b)) FPREV(FNEXT(b)) = FPREV(b);
    if (!g_bins[i]) g_binmap[i >> 6] &= ~(1ULL << (i & 63));
}

__mblock_t *__cervus_bin_fit(size_t need)
{
    int i = bin_of(need);
    for (__mblock_t *b = g_bins[i]; b; b = FNEXT(b))
        if (MB_SIZE(b) >= need) return b;
    for (int j = i + 1; j < NBINS; ) {
        uint64_t m = g_binmap[j >> 6] >> (j & 63);
        if (!m) { j = (j | 63) + 1; continue; }
        j += __builtin_ctzll(m);
        return g_bins[j];
    }
    return NULL;
}

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
        __cervus_bin_remove(prev);
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
    size_t rest_sz = cur - need;
    rest->size      = rest_sz | MB_FREE_BIT;
    rest->prev_size = need;

    __mblock_t *after = __cervus_mb_next(rest);
    if (after != __cervus_heap_end && MB_IS_FREE(after)) {
        __cervus_bin_remove(after);
        rest_sz += MB_SIZE(after);
        rest->size = rest_sz | MB_FREE_BIT;
        after = __cervus_mb_next(rest);
    }
    after->prev_size = rest_sz;
    __cervus_bin_insert(rest);
}

void __cervus_mb_release(__mblock_t *b)
{
    size_t sz = MB_SIZE(b);
    __mblock_t *next = (__mblock_t *)((char *)b + sz);
    if (next != __cervus_heap_end && MB_IS_FREE(next)) {
        __cervus_bin_remove(next);
        sz += MB_SIZE(next);
    }
    __mblock_t *prev = __cervus_mb_prev(b);
    if (prev && MB_IS_FREE(prev)) {
        __cervus_bin_remove(prev);
        sz += MB_SIZE(prev);
        b = prev;
    }
    b->size = sz | MB_FREE_BIT;
    __cervus_mb_next(b)->prev_size = sz;
    __cervus_bin_insert(b);
}
