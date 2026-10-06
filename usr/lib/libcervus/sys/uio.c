#include <sys/uio.h>
#include <sys/syscall.h>
#include <libcervus.h>

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    int cp = __cervus_cp_enter();
    ssize_t r = (ssize_t)__cervus_sys_ret(syscall3(SYS_READV, (uint64_t)fd, (uint64_t)iov, (uint64_t)iovcnt));
    __cervus_cp_leave(cp);
    return r;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    int cp = __cervus_cp_enter();
    ssize_t r = (ssize_t)__cervus_sys_ret(syscall3(SYS_WRITEV, (uint64_t)fd, (uint64_t)iov, (uint64_t)iovcnt));
    __cervus_cp_leave(cp);
    return r;
}

ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t off)
{
    int cp = __cervus_cp_enter();
    ssize_t r = (ssize_t)__cervus_sys_ret(syscall4(SYS_PREADV, (uint64_t)fd, (uint64_t)iov, (uint64_t)iovcnt, (uint64_t)off));
    __cervus_cp_leave(cp);
    return r;
}

ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t off)
{
    int cp = __cervus_cp_enter();
    ssize_t r = (ssize_t)__cervus_sys_ret(syscall4(SYS_PWRITEV, (uint64_t)fd, (uint64_t)iov, (uint64_t)iovcnt, (uint64_t)off));
    __cervus_cp_leave(cp);
    return r;
}
