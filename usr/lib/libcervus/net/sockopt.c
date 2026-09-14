#include <sys/socket.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

int shutdown(int fd, int how)
{
    return (int)__cervus_sys_ret(
        (long)syscall2(SYS_SHUTDOWN_SOCK, (unsigned long)fd, (unsigned long)how));
}

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    return (int)__cervus_sys_ret(
        (long)syscall5(SYS_SETSOCKOPT, (unsigned long)fd, (unsigned long)level,
                       (unsigned long)optname, (unsigned long)optval,
                       (unsigned long)optlen));
}

int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    return (int)__cervus_sys_ret(
        (long)syscall5(SYS_GETSOCKOPT, (unsigned long)fd, (unsigned long)level,
                       (unsigned long)optname, (unsigned long)optval,
                       (unsigned long)optlen));
}
