#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int symlink(const char *target, const char *linkpath)
{
    if (!target || !linkpath) { __cervus_errno = EFAULT; return -1; }
    return (int)__cervus_sys_ret(syscall2(SYS_SYMLINK, target, linkpath));
}
