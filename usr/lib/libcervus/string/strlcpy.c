#include <string.h>
#include <stddef.h>

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t srclen = 0;
    while (src[srclen]) srclen++;
    if (size > 0) {
        size_t n = (srclen >= size) ? size - 1 : srclen;
        for (size_t i = 0; i < n; i++) dst[i] = src[i];
        dst[n] = '\0';
    }
    return srclen;
}
