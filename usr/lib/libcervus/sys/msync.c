#include <sys/mman.h>
#include <errno.h>

int msync(void *addr, size_t len, int flags)
{
    (void)len; (void)flags;
    if (!addr) { errno = EINVAL; return -1; }
    return 0;
}
