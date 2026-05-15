#include <stdlib.h>
#include <stddef.h>

static void __qswap(void *a, void *b, size_t sz)
{
    unsigned char tmp;
    unsigned char *pa = (unsigned char *)a;
    unsigned char *pb = (unsigned char *)b;
    while (sz--) { tmp = *pa; *pa++ = *pb; *pb++ = tmp; }
}

void qsort(void *base, size_t nmemb, size_t sz, int (*cmp)(const void *, const void *))
{
    if (nmemb < 2) return;
    unsigned char *arr = (unsigned char *)base;
    unsigned char *pivot = arr + (nmemb - 1) * sz;
    size_t i = 0;
    for (size_t j = 0; j < nmemb - 1; j++) {
        if (cmp(arr + j * sz, pivot) <= 0) {
            if (i != j) __qswap(arr + i * sz, arr + j * sz, sz);
            i++;
        }
    }
    __qswap(arr + i * sz, pivot, sz);
    qsort(arr, i, sz, cmp);
    qsort(arr + (i + 1) * sz, nmemb - i - 1, sz, cmp);
}
