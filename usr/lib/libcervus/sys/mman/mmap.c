#include <sys/mman.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <libcervus.h>

void *mmap(void *a, size_t l, int p, int f, int fd, off_t o)
{
    long r = syscall6(SYS_MMAP, a, l, p, f, fd, (uint64_t)o);
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return MAP_FAILED; }
    return (void *)r;
}
