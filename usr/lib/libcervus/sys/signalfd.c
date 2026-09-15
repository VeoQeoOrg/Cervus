#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <stdint.h>

extern long __cervus_sys_ret(long r);

int signalfd(int fd, const sigset_t *mask, int flags)
{
    uint64_t m = 0;
    if (mask) m = (uint64_t)mask->__bits[0];
    return (int)__cervus_sys_ret(
        (long)syscall3(SYS_SIGNALFD, (unsigned long)fd,
                       (unsigned long)&m, (unsigned long)flags));
}
