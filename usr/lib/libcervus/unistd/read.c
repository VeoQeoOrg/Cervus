#include <unistd.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <libcervus.h>

ssize_t read(int fd, void *buf, size_t n)
{
    if (fd == 0) __cervus_fflush(stdout);
    return (ssize_t)__cervus_sys_ret(syscall3(SYS_READ, fd, buf, n));
}
