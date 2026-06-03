#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    if (!path || !buf || bufsiz == 0) { __cervus_errno = EINVAL; return -1; }
    return (ssize_t)__cervus_sys_ret(syscall3(SYS_READLINK, path, buf, bufsiz));
}
