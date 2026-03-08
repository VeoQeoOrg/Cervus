#include <stdlib.h>
#include "../../../kernel/include/memory/pmm.h"

void *malloc(size_t size) {
    return kmalloc(size);
}