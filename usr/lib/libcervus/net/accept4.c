#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

int accept4(int fd, struct sockaddr *addr, socklen_t *len, int flags)
{
    if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK)) { errno = EINVAL; return -1; }
    int s = accept(fd, addr, len);
    if (s < 0) return -1;
    if ((flags & SOCK_CLOEXEC) && fcntl(s, F_SETFD, FD_CLOEXEC) < 0) goto fail;
    if (flags & SOCK_NONBLOCK) {
        int fl = fcntl(s, F_GETFL);
        if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) < 0) goto fail;
    }
    return s;
fail:;
    int e = errno;
    close(s);
    errno = e;
    return -1;
}
