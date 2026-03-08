#include <stdlib.h>
#include <string.h>
#include "../../../kernel/include/memory/pmm.h"

void *realloc(void *ptr, size_t size) {
    return krealloc(ptr, size);
}