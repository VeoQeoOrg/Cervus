#include <libcervus.h>
#include <unistd.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    int __cp = __cervus_cp_enter();
    ssize_t __r = (ssize_t)__cervus_sys_ret(
        (long)syscall4(SYS_PREAD, (unsigned long)fd, (unsigned long)buf,
                       (unsigned long)count, (unsigned long)offset));
    __cervus_cp_leave(__cp);
    return __r;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    int __cp = __cervus_cp_enter();
    ssize_t __r = (ssize_t)__cervus_sys_ret(
        (long)syscall4(SYS_PWRITE, (unsigned long)fd, (unsigned long)buf,
                       (unsigned long)count, (unsigned long)offset));
    __cervus_cp_leave(__cp);
    return __r;
}
