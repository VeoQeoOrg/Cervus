#include <pty.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

int openpty(int *master, int *slave, char *name,
            const struct termios *termp, const struct winsize *winp)
{
    int fds[2];
    long r = __cervus_sys_ret(syscall1(SYS_OPENPTY, (uint64_t)(uintptr_t)fds));
    if (r < 0) return -1;
    if (termp) tcsetattr(fds[1], TCSANOW, termp);
    if (winp) ioctl(fds[1], TIOCSWINSZ, winp);
    if (name) {
        unsigned int n = 0;
        if (ioctl(fds[0], TIOCGPTN, &n) == 0) sprintf(name, "/dev/pts/%u", n);
        else name[0] = 0;
    }
    if (master) *master = fds[0];
    if (slave) *slave = fds[1];
    return 0;
}

int login_tty(int fd)
{
    setsid();
    if (dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) return -1;
    if (fd > 2) close(fd);
    return 0;
}

pid_t forkpty(int *master, char *name,
              const struct termios *termp, const struct winsize *winp)
{
    int m, s;
    if (openpty(&m, &s, name, termp, winp) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(m);
        close(s);
        return -1;
    }
    if (pid == 0) {
        close(m);
        if (login_tty(s) < 0) _exit(1);
        return 0;
    }
    close(s);
    if (master) *master = m;
    return pid;
}

int ptsname_r(int fd, char *buf, size_t len)
{
    unsigned int n = 0;
    if (ioctl(fd, TIOCGPTN, &n) != 0) return errno ? errno : ENOTTY;
    if ((size_t)snprintf(buf, len, "/dev/pts/%u", n) >= len) return ERANGE;
    return 0;
}

char *ptsname(int fd)
{
    static char name[32];
    int e = ptsname_r(fd, name, sizeof name);
    if (e) { __cervus_set_errno(e); return NULL; }
    return name;
}

int grantpt(int fd)
{
    unsigned int n;
    return ioctl(fd, TIOCGPTN, &n) == 0 ? 0 : -1;
}

int unlockpt(int fd)
{
    return grantpt(fd);
}

int posix_openpt(int flags)
{
    int m, s;
    if (openpty(&m, &s, NULL, NULL, NULL) != 0) return -1;
    close(s);
    if (flags & O_CLOEXEC) fcntl(m, F_SETFD, FD_CLOEXEC);
    return m;
}
