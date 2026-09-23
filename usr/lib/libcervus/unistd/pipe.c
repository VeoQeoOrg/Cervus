#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int pipe(int fds[2])
{
    return (int)__cervus_sys_ret(syscall1(SYS_PIPE, fds));
}

int pipe2(int fds[2], int flags)
{
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) {
        __cervus_errno = EINVAL;
        return -1;
    }
    if (pipe(fds) < 0) return -1;
    for (int i = 0; i < 2; i++) {
        if (flags & O_CLOEXEC) fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        if (flags & O_NONBLOCK) fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
    }
    return 0;
}

int dup3(int oldfd, int newfd, int flags)
{
    if (oldfd == newfd || (flags & ~O_CLOEXEC)) {
        __cervus_errno = EINVAL;
        return -1;
    }
    int r = dup2(oldfd, newfd);
    if (r >= 0 && (flags & O_CLOEXEC)) fcntl(r, F_SETFD, FD_CLOEXEC);
    return r;
}
