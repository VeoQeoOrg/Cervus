#include <string.h>

char* strncat(char* restrict dst, const char* restrict src, size_t n) {
    char* d = dst + strlen(dst);
    while (n-- && *src)
        *d++ = *src++;
    *d = '\0';
    return dst;
}