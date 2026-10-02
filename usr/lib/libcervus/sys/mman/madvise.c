#include <sys/mman.h>
#include <sys/syscall.h>
#include <errno.h>
#include <libcervus.h>

int madvise(void *addr, size_t len, int advice)
{
    return (int)__cervus_sys_ret(syscall3(SYS_MADVISE, addr, len, advice));
}

int posix_madvise(void *addr, size_t len, int advice)
{
    int saved = errno;
    int r = madvise(addr, len, advice == 4 ? 0 : advice);
    if (r < 0) { r = errno; errno = saved; return r; }
    return 0;
}
