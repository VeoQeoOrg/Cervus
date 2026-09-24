#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/mman_shared.h>
#include <unistd.h>
#include <errno.h>

static int ret_or_errno(long r)
{
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}

int eventfd(unsigned int initval, int flags)
{
    return ret_or_errno((long)syscall3(SYS_EVENTFD, initval, (uint64_t)flags, 0));
}

int eventfd_read(int fd, eventfd_t *value)
{
    eventfd_t tmp;
    ssize_t n = read(fd, &tmp, sizeof tmp);
    if (n != (ssize_t)sizeof tmp) return -1;
    if (value) *value = tmp;
    return 0;
}

int eventfd_write(int fd, eventfd_t value)
{
    ssize_t n = write(fd, &value, sizeof value);
    return (n == (ssize_t)sizeof value) ? 0 : -1;
}

int timerfd_create(int clockid, int flags)
{
    return ret_or_errno((long)syscall3(SYS_TIMERFD_CREATE,
                                       (uint64_t)clockid, (uint64_t)flags, 0));
}

static void itimerspec_from_raw(struct itimerspec *v, const uint64_t raw[4])
{
    v->it_interval.tv_sec  = (time_t)raw[0];
    v->it_interval.tv_nsec = (long)raw[1];
    v->it_value.tv_sec     = (time_t)raw[2];
    v->it_value.tv_nsec    = (long)raw[3];
}

int timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
                    struct itimerspec *old_value)
{
    if (!new_value) { errno = EFAULT; return -1; }
    uint64_t spec[4] = {
        (uint64_t)new_value->it_interval.tv_sec,
        (uint64_t)new_value->it_interval.tv_nsec,
        (uint64_t)new_value->it_value.tv_sec,
        (uint64_t)new_value->it_value.tv_nsec,
    };
    uint64_t out[4] = { 0, 0, 0, 0 };
    long r = (long)syscall4(SYS_TIMERFD_SETTIME2, (uint64_t)fd, (uint64_t)flags,
                            (uint64_t)(uintptr_t)spec,
                            old_value ? (uint64_t)(uintptr_t)out : 0);
    if (r < 0) { errno = (int)-r; return -1; }
    if (old_value) itimerspec_from_raw(old_value, out);
    return 0;
}

int timerfd_gettime(int fd, struct itimerspec *curr_value)
{
    if (!curr_value) { errno = EFAULT; return -1; }
    uint64_t out[4] = { 0, 0, 0, 0 };
    long r = (long)syscall2(SYS_TIMERFD_GETTIME, (uint64_t)fd, (uint64_t)(uintptr_t)out);
    if (r < 0) { errno = (int)-r; return -1; }
    itimerspec_from_raw(curr_value, out);
    return 0;
}

int epoll_create(int size)
{
    (void)size;
    return ret_or_errno((long)syscall3(SYS_EPOLL_CREATE, 0, 0, 0));
}

int epoll_create1(int flags)
{
    return ret_or_errno((long)syscall3(SYS_EPOLL_CREATE, (uint64_t)flags, 0, 0));
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    struct { int fd; uint64_t evptr; } args = { fd, (uint64_t)(uintptr_t)event };
    return ret_or_errno((long)syscall3(SYS_EPOLL_CTL, (uint64_t)epfd,
                                       (uint64_t)op, (uint64_t)(uintptr_t)&args));
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    struct { int maxevents; int timeout_ms; } args = { maxevents, timeout };
    return ret_or_errno((long)syscall3(SYS_EPOLL_WAIT, (uint64_t)epfd,
                                       (uint64_t)(uintptr_t)events,
                                       (uint64_t)(uintptr_t)&args));
}

int socketpair(int domain, int type, int protocol, int fds[2])
{
    (void)protocol;
    return ret_or_errno((long)syscall3(SYS_SOCKETPAIR, (uint64_t)domain,
                                       (uint64_t)type, (uint64_t)(uintptr_t)fds));
}

int memfd_create(const char *name, unsigned int flags)
{
    return ret_or_errno((long)syscall3(SYS_MEMFD_CREATE,
                                       (uint64_t)(uintptr_t)name, flags, 0));
}

long sendmsg(int fd, const struct msghdr *msg, int flags)
{
    long r = (long)syscall3(SYS_SENDMSG, (uint64_t)fd,
                            (uint64_t)(uintptr_t)msg, (uint64_t)flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return r;
}

long recvmsg(int fd, struct msghdr *msg, int flags)
{
    long r = (long)syscall3(SYS_RECVMSG, (uint64_t)fd,
                            (uint64_t)(uintptr_t)msg, (uint64_t)flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return r;
}
