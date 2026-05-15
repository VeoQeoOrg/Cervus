#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int fstat(int fd, struct stat *out)
{
    return (int)__cervus_sys_ret(syscall2(SYS_FSTAT, fd, out));
}
