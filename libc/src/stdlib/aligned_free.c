#include <stdlib.h>
#include "../../../kernel/include/memory/pmm.h"

void aligned_free(void* ptr) {
    if (!ptr) return;
    void* raw = ((void**)ptr)[-1];
    kfree(raw);
}
