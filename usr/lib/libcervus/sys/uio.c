#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    if (!iov || iovcnt < 0 || iovcnt > IOV_MAX) { errno = EINVAL; return -1; }

    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        ssize_t n = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return total ? total : -1;
        total += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    if (!iov || iovcnt < 0 || iovcnt > IOV_MAX) { errno = EINVAL; return -1; }

    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        size_t off = 0;
        while (off < iov[i].iov_len) {
            ssize_t n = write(fd, (char *)iov[i].iov_base + off, iov[i].iov_len - off);
            if (n < 0) return total ? total : -1;
            if (n == 0) return total;
            off += (size_t)n;
            total += n;
        }
    }
    return total;
}
