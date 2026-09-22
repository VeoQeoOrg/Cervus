#include <sys/mman.h>

int madvise(void *addr, size_t len, int advice)
{
    (void)addr; (void)len; (void)advice;
    return 0;
}

int posix_madvise(void *addr, size_t len, int advice)
{
    (void)addr; (void)len; (void)advice;
    return 0;
}
