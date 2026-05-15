#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <libcervus.h>

int tcsetattr(int fd, int optional_actions, const struct termios *t)
{
    if (!t) { __cervus_errno = EINVAL; return -1; }
    unsigned long req;
    switch (optional_actions) {
        case TCSADRAIN: req = TCSETSW; break;
        case TCSAFLUSH: req = TCSETSF; break;
        case TCSANOW:
        default:        req = TCSETS;  break;
    }
    return (int)__cervus_sys_ret(syscall3(SYS_IOCTL, fd, req, t));
}
