#include <stdlib.h>
#include <stddef.h>

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *))
{
    const unsigned char *lo = (const unsigned char *)base;
    const unsigned char *hi = lo + nmemb * size;
    while (lo < hi) {
        size_t half = (size_t)((hi - lo) / (ptrdiff_t)size) / 2;
        const unsigned char *mid = lo + half * size;
        int r = cmp(key, mid);
        if (r == 0) return (void *)mid;
        if (r < 0) hi = mid;
        else lo = mid + size;
    }
    return NULL;
}
