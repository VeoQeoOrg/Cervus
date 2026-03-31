#include <stdlib.h>
#include "../../../kernel/include/memory/pmm.h"

void *malloc(size_t size) {
    if (size == 0) return NULL;
    return kmalloc(size);
}
