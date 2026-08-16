#include <sys/stat.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

int chmod(const char *path, mode_t mode)
{
    return (int)__cervus_sys_ret(
        (long)syscall2(SYS_CHMOD, (unsigned long)path, (unsigned long)mode));
}
