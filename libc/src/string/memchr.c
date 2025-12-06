#include <string.h>

void *memchr(void *ptr, int val, size_t n) {
    const unsigned char *p = (const unsigned char*)ptr;
    unsigned char c = (unsigned char)val;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == c)
            return (void*)(p + i);
    }

    return NULL;
}
