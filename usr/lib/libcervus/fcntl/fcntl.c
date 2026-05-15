#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <libcervus.h>

int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);
    return (int)__cervus_sys_ret(syscall3(SYS_FCNTL, fd, cmd, arg));
}
