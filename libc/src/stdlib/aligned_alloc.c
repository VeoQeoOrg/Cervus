#include <stdlib.h>
#include <stdint.h>

void *aligned_alloc(size_t alignment, size_t size) {
    if (size == 0) return NULL;
    if (alignment == 0 || (alignment & (alignment - 1))) return NULL;
    if (alignment < 16) alignment = 16;

    void *raw = malloc(size + alignment + sizeof(void *));
    if (!raw) return NULL;

    uintptr_t addr    = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);

    ((void **)aligned)[-1] = raw;

    return (void *)aligned;
}