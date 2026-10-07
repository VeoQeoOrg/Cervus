#include <sys/socket.h>
#include <sys/syscall.h>
#include <libcervus.h>

int accept4(int fd, struct sockaddr *addr, socklen_t *len, int flags)
{
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret(syscall4(SYS_ACCEPT4, fd, addr, len, flags));
    __cervus_cp_leave(__cp);
    return __r;
}
