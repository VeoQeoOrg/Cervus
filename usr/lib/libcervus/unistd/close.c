#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int close(int fd)
{
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret(syscall1(SYS_CLOSE, fd));
    __cervus_cp_leave(__cp);
    return __r;
}

int close_range(unsigned int first, unsigned int last, int flags)
{
    return (int)__cervus_sys_ret(syscall3(SYS_CLOSE_RANGE, first, last, (unsigned int)flags));
}

void closefrom(int lowfd)
{
    if (lowfd < 0) lowfd = 0;
    close_range((unsigned int)lowfd, ~0u, 0);
}
