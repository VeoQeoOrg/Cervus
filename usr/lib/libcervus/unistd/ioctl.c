#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <stdarg.h>
#include <libcervus.h>

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return (int)__cervus_sys_ret(syscall3(SYS_IOCTL, fd, request, arg));
}
