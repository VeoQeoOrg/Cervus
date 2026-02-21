#ifndef _HEAP_H
#define _HEAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../../kernel/include/memory/pmm.h"

#define HEAP_MAGIC            0xDEADBEEFCAFEBABEULL
#define HEAP_ALIGNMENT        16
#define HEAP_BIN_COUNT        32
#define HEAP_LARGE_THRESHOLD  (PAGE_SIZE * 4)

typedef struct heap_block {
    size_t             size;
    size_t             magic;
    struct heap_block *next_free;
    bool               free;
    bool               large;
    uint8_t            _pad[6];
} heap_block_t;

_Static_assert(sizeof(heap_block_t) == 32, "heap_block_t must be 32 bytes");

extern heap_block_t *_heap_bins[HEAP_BIN_COUNT];
extern bool          _heap_ready;

static inline size_t _heap_align_up(size_t v, size_t a) {
    return (v + a - 1) & ~(a - 1);
}

static inline size_t _heap_bin_of(size_t size) {
    size_t i = 0;
    while (size > 1 && i < HEAP_BIN_COUNT -1) {
        size >>= 1;
        i++;
    }
    return i;
}

#endif