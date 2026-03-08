#include <stdlib.h>
#include "../../../kernel/include/memory/pmm.h"

void free(void *ptr) {
    kfree(ptr);
}