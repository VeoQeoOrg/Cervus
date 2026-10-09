#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

ssize_t read(int fd, void *buf, size_t n)
{
    int __cp = __cervus_cp_enter();
    ssize_t __r = (ssize_t)__cervus_sys_ret(syscall3(SYS_READ, fd, buf, n));
    __cervus_cp_leave(__cp);
    return __r;
}
