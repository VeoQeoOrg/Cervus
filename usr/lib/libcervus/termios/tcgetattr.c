#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <libcervus.h>

int tcgetattr(int fd, struct termios *t)
{
    if (!t) { __cervus_errno = EINVAL; return -1; }
    return (int)__cervus_sys_ret(syscall3(SYS_IOCTL, fd, TCGETS, t));
}
