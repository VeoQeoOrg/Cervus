#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int fsync(int fd)
{
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret(syscall1(SYS_FSYNC, fd));
    __cervus_cp_leave(__cp);
    return __r;
}

int fdatasync(int fd)
{
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret(syscall1(SYS_FDATASYNC, fd));
    __cervus_cp_leave(__cp);
    return __r;
}
