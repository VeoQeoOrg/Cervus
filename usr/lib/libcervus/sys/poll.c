#include <poll.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <libcervus.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    return (int)__cervus_sys_ret(syscall3(SYS_POLL, fds, nfds, timeout));
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    return (int)__cervus_sys_ret(syscall5(SYS_SELECT, nfds, readfds, writefds, exceptfds, timeout));
}
