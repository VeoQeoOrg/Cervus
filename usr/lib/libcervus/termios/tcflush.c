#include <termios.h>
#include <sys/syscall.h>
#include <libcervus.h>

int tcflush(int fd, int queue)
{
    return (int)__cervus_sys_ret(syscall3(SYS_IOCTL, fd, TCFLSH, (long)queue));
}

int tcdrain(int fd)
{
    return (int)__cervus_sys_ret(syscall3(SYS_IOCTL, fd, TCDRAIN, 0));
}

int tcflow(int fd, int action)
{
    return (int)__cervus_sys_ret(syscall3(SYS_IOCTL, fd, TCXONC, (long)action));
}

int tcsendbreak(int fd, int duration)
{
    return (int)__cervus_sys_ret(syscall3(SYS_IOCTL, fd, TCSBRK, (long)duration));
}

pid_t tcgetsid(int fd)
{
    (void)fd;
    return (pid_t)1;
}
