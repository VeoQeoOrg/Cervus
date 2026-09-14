#include <unistd.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    return (ssize_t)__cervus_sys_ret(
        (long)syscall4(SYS_PREAD, (unsigned long)fd, (unsigned long)buf,
                       (unsigned long)count, (unsigned long)offset));
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    return (ssize_t)__cervus_sys_ret(
        (long)syscall4(SYS_PWRITE, (unsigned long)fd, (unsigned long)buf,
                       (unsigned long)count, (unsigned long)offset));
}
