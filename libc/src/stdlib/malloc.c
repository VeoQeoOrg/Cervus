#include <stdlib.h>
#include "heap.h"
#include "../../../kernel/include/io/serial.h"

static heap_block_t *find_free(size_t size) {
    for (size_t i = _heap_bin_of(size); i < HEAP_BIN_COUNT; i++) {
        heap_block_t **p = &_heap_bins[i];
        heap_block_t  *b =  _heap_bins[i];
        while (b) {
            if (b->size >= size) {
                *p = b->next_free;
                b->next_free = NULL;
                b->free = false;
                return b;
            }
            p = &b->next_free;
            b =  b->next_free;
        }
    }
    return NULL;
}

void *malloc(size_t size) {
    if (!_heap_ready) malloc_init();
    if (size == 0) return NULL;

    size = _heap_align_up(size, HEAP_ALIGNMENT);

    if (size > HEAP_LARGE_THRESHOLD) {
        size_t pages = _heap_align_up(sizeof(heap_block_t) + size, PAGE_SIZE) / PAGE_SIZE;
        heap_block_t *b = (heap_block_t *)pmm_alloc_zero(pages);
        if (!b) return NULL;
        b->size      = pages * PAGE_SIZE - sizeof(heap_block_t);
        b->magic     = HEAP_MAGIC;
        b->next_free = NULL;
        b->free      = false;
        b->large     = true;
        return (void *)(b + 1);
    }

    heap_block_t *b = find_free(size);
    if (b) return (void *)(b + 1);

    size_t pages = _heap_align_up(sizeof(heap_block_t) + size, PAGE_SIZE) / PAGE_SIZE;
    b = (heap_block_t *)pmm_alloc_zero(pages);
    if (!b) {
        serial_printf("[MALLOC] out of memory (size=%zu)\n", size);
        return NULL;
    }
    b->size      = pages * PAGE_SIZE - sizeof(heap_block_t);
    b->magic     = HEAP_MAGIC;
    b->next_free = NULL;
    b->free      = false;
    b->large     = false;
    return (void *)(b + 1);
}