#include <stdlib.h>

void aligned_free(void* ptr) {
    if (!ptr) return;
    void* raw = ((void**)ptr)[-1];
    free(raw);
}