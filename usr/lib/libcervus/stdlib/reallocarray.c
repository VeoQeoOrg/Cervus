#include <stdlib.h>
#include <errno.h>
#include <libcervus.h>

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
    size_t total;
    if (__builtin_mul_overflow(nmemb, size, &total)) {
        __cervus_set_errno(ENOMEM);
        return NULL;
    }
    return realloc(ptr, total);
}
