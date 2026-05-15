#include <string.h>
#include <stddef.h>

void *memmem(const void *haystack, size_t hlen, const void *needle, size_t nlen)
{
    if (nlen == 0) return (void *)haystack;
    if (hlen < nlen) return NULL;
    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;
    size_t last = hlen - nlen;
    for (size_t i = 0; i <= last; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, nlen) == 0)
            return (void *)(h + i);
    }
    return NULL;
}
