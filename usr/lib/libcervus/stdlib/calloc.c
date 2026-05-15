#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

void *calloc(size_t nm, size_t sz)
{
    size_t t = nm * sz;
    if (nm && t / nm != sz) { __cervus_errno = ENOMEM; return NULL; }
    void *p = malloc(t);
    if (p) memset(p, 0, t);
    return p;
}
