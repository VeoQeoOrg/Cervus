#include <stdlib.h>
#include <string.h>
#include "../../../kernel/include/memory/pmm.h"

void *calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    if (nmemb > (size_t)-1 / size) return NULL;
    void *ptr = kmalloc(nmemb * size);
    if (ptr) memset(ptr, 0, nmemb * size);
    return ptr;
}
