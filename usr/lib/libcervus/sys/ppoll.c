#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <libcervus.h>

int ppoll(struct pollfd *fds, nfds_t n, const struct timespec *to, const sigset_t *mask)
{
    return (int)__cervus_sys_ret(syscall5(SYS_PPOLL, fds, n, to, mask, sizeof(uint64_t)));
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const sigset_t *mask)
{
    return (int)__cervus_sys_ret(__syscall6(SYS_PSELECT, (uint64_t)nfds, (uint64_t)readfds, (uint64_t)writefds,
                                            (uint64_t)exceptfds, (uint64_t)timeout, (uint64_t)mask));
}
