#include <sys/stat.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

int fchmod(int fd, mode_t mode)
{
    return (int)__cervus_sys_ret(
        (long)syscall2(SYS_FCHMOD, (unsigned long)fd, (unsigned long)mode));
}
