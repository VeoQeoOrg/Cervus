#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

ssize_t write(int fd, const void *buf, size_t n)
{
    return (ssize_t)__cervus_sys_ret(syscall3(SYS_WRITE, fd, buf, n));
}
