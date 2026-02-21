#include <stdlib.h>
#include "heap.h"
#include "../../../kernel/include/io/serial.h"

void free(void *ptr) {
    if (!ptr) return;

    heap_block_t *b = (heap_block_t *)ptr - 1;

    if (b->magic != HEAP_MAGIC) {
        serial_printf("[MALLOC ERROR] free() corrupt block at %p\n", ptr);
        return;
    }
    if (b->free) {
        serial_printf("[MALLOC WARNING] double-free at %p\n", ptr);
        return;
    }

    if (b->large) {
        size_t pages = _heap_align_up(sizeof(heap_block_t) + b->size, PAGE_SIZE) / PAGE_SIZE;
        pmm_free(b, pages);
        return;
    }

    b->free = true;
    size_t idx = _heap_bin_of(b->size);
    b->next_free    = _heap_bins[idx];
    _heap_bins[idx] = b;
}