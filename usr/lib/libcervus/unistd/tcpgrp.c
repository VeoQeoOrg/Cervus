#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

pid_t tcgetpgrp(int fd)
{
    int pg = 0;
    if (ioctl(fd, TIOCGPGRP, &pg) != 0) return -1;
    return (pid_t)pg;
}

int tcsetpgrp(int fd, pid_t pgrp)
{
    int pg = (int)pgrp;
    return ioctl(fd, TIOCSPGRP, &pg);
}
