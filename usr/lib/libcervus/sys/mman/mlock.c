#include <sys/mman.h>
#include <sys/syscall.h>
#include <libcervus.h>

int mlock(const void *addr, size_t len)
{
    return (int)__cervus_sys_ret(syscall2(SYS_MLOCK, addr, len));
}

int munlock(const void *addr, size_t len)
{
    return (int)__cervus_sys_ret(syscall2(SYS_MUNLOCK, addr, len));
}

int mlockall(int flags)
{
    return (int)__cervus_sys_ret(syscall1(SYS_MLOCKALL, flags));
}

int munlockall(void)
{
    return (int)__cervus_sys_ret(syscall0(SYS_MUNLOCKALL));
}
