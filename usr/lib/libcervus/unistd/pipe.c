#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int pipe(int fds[2])
{
    return (int)__cervus_sys_ret(syscall1(SYS_PIPE, fds));
}
