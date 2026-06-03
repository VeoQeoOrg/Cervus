#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

long getdents(int fd, void *buf, unsigned long count)
{
    return (long)__cervus_sys_ret(syscall3(SYS_GETDENTS, fd, buf, count));
}
