#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <termios.h>
#include <libcervus.h>

int isatty(int fd)
{
    struct winsize ws;
    long r = syscall3(SYS_IOCTL, fd, TIOCGWINSZ, &ws);
    if (r < 0) {
        __cervus_errno = (int)-r;
        return 0;
    }
    return 1;
}
