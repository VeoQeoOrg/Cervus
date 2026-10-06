#include <poll.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <libcervus.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret(syscall3(SYS_POLL, fds, nfds, timeout));
    __cervus_cp_leave(__cp);
    return __r;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret(syscall5(SYS_SELECT, nfds, readfds, writefds, exceptfds, timeout));
    __cervus_cp_leave(__cp);
    return __r;
}
