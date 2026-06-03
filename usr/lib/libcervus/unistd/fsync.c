#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int fsync(int fd)
{
    return (int)__cervus_sys_ret(syscall1(SYS_FSYNC, fd));
}

int fdatasync(int fd)
{
    return (int)__cervus_sys_ret(syscall1(SYS_FDATASYNC, fd));
}
