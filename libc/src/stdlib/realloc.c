#include <stdlib.h>
#include <string.h>
#include "heap.h"
#include "../../../kernel/include/io/serial.h"

void *realloc(void *ptr, size_t size) {
    if (!ptr)  return malloc(size);
    if (!size) { free(ptr); return NULL; }

    heap_block_t *b = (heap_block_t *)ptr - 1;
    if (b->magic != HEAP_MAGIC) {
        serial_printf("[MALLOC ERROR] realloc() corrupt block at %p\n", ptr);
        return NULL;
    }

    if (b->size >= _heap_align_up(size, HEAP_ALIGNMENT))
        return ptr;

    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;

    memcpy(new_ptr, ptr, b->size < size ? b->size : size);
    free(ptr);
    return new_ptr;
}