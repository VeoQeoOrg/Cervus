#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int truncate(const char *path, off_t length)
{
    if (!path) { __cervus_errno = EFAULT; return -1; }
    return (int)__cervus_sys_ret(syscall2(SYS_TRUNCATE, path, (uint64_t)length));
}

int ftruncate(int fd, off_t length)
{
    return (int)__cervus_sys_ret(syscall2(SYS_FTRUNCATE, fd, (uint64_t)length));
}
