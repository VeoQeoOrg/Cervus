#include <poll.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

int ppoll(struct pollfd *fds, nfds_t n, const struct timespec *to, const sigset_t *mask)
{
    int ms = -1;
    if (to) {
        long long v = (long long)to->tv_sec * 1000 + to->tv_nsec / 1000000;
        if (v < 0) { errno = EINVAL; return -1; }
        if (v > 0x7fffffffLL) v = 0x7fffffffLL;
        ms = (int)v;
    }

    sigset_t old;
    int swapped = (mask && sigprocmask(SIG_SETMASK, mask, &old) == 0);
    int r = poll(fds, n, ms);
    if (swapped) sigprocmask(SIG_SETMASK, &old, 0);
    return r;
}
