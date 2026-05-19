#include <string.h>
#include <stddef.h>

size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dlen = 0;
    while (dlen < size && dst[dlen]) dlen++;
    size_t slen = 0;
    while (src[slen]) slen++;
    if (dlen == size) return size + slen;
    size_t space = size - dlen - 1;
    size_t n = (slen < space) ? slen : space;
    for (size_t i = 0; i < n; i++) dst[dlen + i] = src[i];
    dst[dlen + n] = '\0';
    return dlen + slen;
}
