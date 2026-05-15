#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int dup2(int oldfd, int newfd)
{
    return (int)__cervus_sys_ret(syscall2(SYS_DUP2, oldfd, newfd));
}
