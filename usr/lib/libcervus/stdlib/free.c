#include <stdlib.h>
#include <stddef.h>
#include <libcervus.h>

void free(void *p)
{
    if (!p) return;
    __cervus_lock(&__cervus_heap_lock);
    __cervus_mb_release(MB_FROM_USER(p));
    __cervus_unlock(&__cervus_heap_lock);
}
