#include <sys/mman.h>
#include <sys/syscall.h>
#include <libcervus.h>

int msync(void *addr, size_t len, int flags)
{
    return (int)__cervus_sys_ret(syscall3(SYS_MSYNC, addr, len, flags));
}
