#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "darwin.h"

#define DNCCS 20

struct dtermios {
    unsigned long c_iflag, c_oflag, c_cflag, c_lflag;
    unsigned char c_cc[DNCCS];
    unsigned long c_ispeed, c_ospeed;
};
_Static_assert(sizeof(struct dtermios) == 72, "Darwin termios is 72 bytes");

struct bitmap {
    unsigned long d;
    unsigned long c;
};

static const struct bitmap imap[] = {
    { 0x1, IGNBRK }, { 0x2, BRKINT }, { 0x4, IGNPAR }, { 0x8, PARMRK }, { 0x10, INPCK },
    { 0x20, ISTRIP }, { 0x40, INLCR }, { 0x80, IGNCR }, { 0x100, ICRNL }, { 0x200, IXON },
    { 0x400, IXOFF }, { 0x800, IXANY }, { 0x2000, IMAXBEL }, { 0x4000, IUTF8 },
};

static const struct bitmap omap[] = {
    { 0x1, OPOST }, { 0x2, ONLCR }, { 0x4, TAB3 }, { 0x10, OCRNL }, { 0x20, ONOCR }, { 0x40, ONLRET },
};

static const struct bitmap cmap[] = {
    { 0x400, CSTOPB }, { 0x800, CREAD }, { 0x1000, PARENB }, { 0x2000, PARODD },
    { 0x4000, HUPCL }, { 0x8000, CLOCAL }, { 0x30000, CRTSCTS },
};

static const struct bitmap lmap[] = {
    { 0x1, ECHOKE }, { 0x2, ECHOE }, { 0x4, ECHOK }, { 0x8, ECHO }, { 0x10, ECHONL },
    { 0x20, ECHOPRT }, { 0x40, ECHOCTL }, { 0x80, ISIG }, { 0x100, ICANON }, { 0x400, IEXTEN },
    { 0x400000, TOSTOP }, { 0x80000000, NOFLSH },
};

static const struct {
    int d, c;
} ccmap[] = {
    { 0, VEOF }, { 1, VEOL }, { 2, VEOL2 }, { 3, VERASE }, { 4, VWERASE }, { 5, VKILL },
    { 6, VREPRINT }, { 8, VINTR }, { 9, VQUIT }, { 10, VSUSP }, { 12, VSTART }, { 13, VSTOP },
    { 14, VLNEXT }, { 15, VDISCARD }, { 16, VMIN }, { 17, VTIME },
};

static const unsigned long bauds[] = {
    0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400,
};

#define N(a) (sizeof(a) / sizeof((a)[0]))

static unsigned long to_d(unsigned long v, const struct bitmap *m, size_t n)
{
    unsigned long r = 0;
    for (size_t i = 0; i < n; i++)
        if ((v & m[i].c) == m[i].c)
            r |= m[i].d;
    return r;
}

static unsigned long to_c(unsigned long v, const struct bitmap *m, size_t n)
{
    unsigned long r = 0;
    for (size_t i = 0; i < n; i++)
        if ((v & m[i].d) == m[i].d)
            r |= m[i].c;
    return r;
}

static unsigned long speed_to_baud(speed_t s)
{
    if (s < N(bauds))
        return bauds[s];
    return 115200;
}

static speed_t baud_to_speed(unsigned long b)
{
    for (size_t i = 0; i < N(bauds); i++)
        if (bauds[i] == b)
            return (speed_t)i;
    return B38400;
}

static void conv_to_d(const struct termios *t, struct dtermios *d)
{
    memset(d, 0, sizeof *d);
    d->c_iflag = to_d(t->c_iflag, imap, N(imap));
    d->c_oflag = to_d(t->c_oflag, omap, N(omap));
    d->c_cflag = to_d(t->c_cflag, cmap, N(cmap)) | (((t->c_cflag & CSIZE) >> 4) << 8);
    d->c_lflag = to_d(t->c_lflag, lmap, N(lmap));
    for (size_t i = 0; i < N(ccmap); i++)
        d->c_cc[ccmap[i].d] = t->c_cc[ccmap[i].c];
    d->c_ispeed = speed_to_baud(cfgetispeed(t));
    d->c_ospeed = speed_to_baud(cfgetospeed(t));
}

static void conv_to_c(const struct dtermios *d, struct termios *t)
{
    memset(t, 0, sizeof *t);
    t->c_iflag = (tcflag_t)to_c(d->c_iflag, imap, N(imap));
    t->c_oflag = (tcflag_t)to_c(d->c_oflag, omap, N(omap));
    t->c_cflag = (tcflag_t)(to_c(d->c_cflag, cmap, N(cmap)) | (((d->c_cflag >> 8) & 3) << 4));
    t->c_lflag = (tcflag_t)to_c(d->c_lflag, lmap, N(lmap));
    for (size_t i = 0; i < N(ccmap); i++)
        t->c_cc[ccmap[i].c] = d->c_cc[ccmap[i].d];
    cfsetispeed(t, baud_to_speed(d->c_ispeed));
    cfsetospeed(t, baud_to_speed(d->c_ospeed));
}

static void tio_log(const char *op, int fd, int act, const struct dtermios *d)
{
    static int lfd = -2;
    if (lfd == -2) {
        const char *p = getenv("DYLD_TRACE_TIO");
        lfd = p && *p ? open(p, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644) : -1;
    }
    if (lfd < 0)
        return;
    char b[160];
    int n = snprintf(b, sizeof b, "%s fd=%d act=%d lflag=%#lx iflag=%#lx vmin=%u vtime=%u\n", op, fd, act,
                     d->c_lflag, d->c_iflag, d->c_cc[16], d->c_cc[17]);
    if (n > 0)
        write(lfd, b, (size_t)n);
}

static int d_tcgetattr(int fd, struct dtermios *d)
{
    struct termios t;
    if (tcgetattr(fd, &t) < 0)
        return -1;
    conv_to_d(&t, d);
    return 0;
}

static int d_tcsetattr(int fd, int act, const struct dtermios *d)
{
    struct termios t;
    act &= ~0x10;
    if (act < 0 || act > 2) {
        errno = EINVAL;
        return -1;
    }
    tio_log("set", fd, act, d);
    conv_to_c(d, &t);
    return tcsetattr(fd, act, &t);
}

static int d_tcflush(int fd, int q)
{
    if (q < 1 || q > 3) {
        errno = EINVAL;
        return -1;
    }
    return tcflush(fd, q - 1);
}

static int d_tcflow(int fd, int a)
{
    if (a < 1 || a > 4) {
        errno = EINVAL;
        return -1;
    }
    return tcflow(fd, a - 1);
}

static unsigned long d_cfgetispeed(const struct dtermios *d) { return d->c_ispeed; }
static unsigned long d_cfgetospeed(const struct dtermios *d) { return d->c_ospeed; }
static int d_cfsetispeed(struct dtermios *d, unsigned long s) { d->c_ispeed = s; return 0; }
static int d_cfsetospeed(struct dtermios *d, unsigned long s) { d->c_ospeed = s; return 0; }

static int d_cfsetspeed(struct dtermios *d, unsigned long s)
{
    d->c_ispeed = d->c_ospeed = s;
    return 0;
}

static void d_cfmakeraw(struct dtermios *d)
{
    d->c_iflag &= ~(0x1ul | 0x2 | 0x8 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x2000);
    d->c_oflag &= ~0x1ul;
    d->c_lflag &= ~(0x8ul | 0x10 | 0x100 | 0x80 | 0x400 | 0x800 | 0x20000000);
    d->c_cflag &= ~(0x300ul | 0x1000);
    d->c_cflag |= 0x300 | 0x800;
    d->c_cc[16] = 1;
    d->c_cc[17] = 0;
}

int d_term_ioctl(int fd, unsigned long req, void *arg, int *handled)
{
    *handled = 1;
    switch (req) {
    case 0x40487413:
        return d_tcgetattr(fd, arg);
    case 0x80487414:
        return d_tcsetattr(fd, TCSANOW, arg);
    case 0x80487415:
        return d_tcsetattr(fd, TCSADRAIN, arg);
    case 0x80487416:
        return d_tcsetattr(fd, TCSAFLUSH, arg);
    case 0x80047410: {
        int q = *(int *)arg;
        return tcflush(fd, !q || ((q & 1) && (q & 2)) ? TCIOFLUSH : (q & 2) ? TCOFLUSH : TCIFLUSH);
    }
    case 0x2000745e:
        return tcdrain(fd);
    }
    *handled = 0;
    return -1;
}

DSYM("_tcgetattr", d_tcgetattr);
DSYM("_tcsetattr", d_tcsetattr);
DSYM("_tcflush", d_tcflush);
DSYM("_tcflow", d_tcflow);
DSYM("_cfgetispeed", d_cfgetispeed);
DSYM("_cfgetospeed", d_cfgetospeed);
DSYM("_cfsetispeed", d_cfsetispeed);
DSYM("_cfsetospeed", d_cfsetospeed);
DSYM("_cfsetspeed", d_cfsetspeed);
DSYM("_cfmakeraw", d_cfmakeraw);
DPASS(tcdrain);
DPASS(tcsendbreak);
DPASS(tcgetsid);

struct dtimeval {
    long tv_sec;
    int tv_usec;
};

static int d_check_fd_set_overflow(int fd, const void *set, int unlimited)
{
    (void)set;
    return fd >= 0 && (unlimited || fd < FD_SETSIZE);
}
DSYM("___darwin_check_fd_set_overflow", d_check_fd_set_overflow);

static int d_select(int n, fd_set *r, fd_set *w, fd_set *e, struct dtimeval *tv)
{
    struct timeval t, *tp = NULL;
    if (tv) {
        t.tv_sec = tv->tv_sec;
        t.tv_usec = tv->tv_usec;
        tp = &t;
    }
    int rc = select(n, r, w, e, tp);
    if (tv) {
        tv->tv_sec = t.tv_sec;
        tv->tv_usec = (int)t.tv_usec;
    }
    return rc;
}
DSYM("_select", d_select);

static int d_pselect(int n, fd_set *r, fd_set *w, fd_set *e, const struct timespec *ts, const uint32_t *mask)
{
    sigset_t m;
    if (mask) {
        sigemptyset(&m);
        for (int i = 1; i < 32; i++)
            if ((*mask & (1u << (i - 1))) && d_sig_d2lx(i))
                sigaddset(&m, d_sig_d2lx(i));
    }
    return pselect(n, r, w, e, ts, mask ? &m : NULL);
}
DSYM("_pselect", d_pselect);

static short ev_to_c(short e)
{
    short r = e & (POLLIN | POLLPRI | POLLOUT | POLLERR | POLLHUP | POLLNVAL | POLLRDNORM | POLLRDBAND);
    if (e & 0x100)
        r |= POLLWRBAND;
    return r;
}

static short ev_to_d(short e)
{
    short r = e & (POLLIN | POLLPRI | POLLOUT | POLLERR | POLLHUP | POLLNVAL | POLLRDNORM | POLLRDBAND);
    if (e & POLLWRNORM)
        r |= POLLOUT;
    if (e & POLLWRBAND)
        r |= 0x100;
    return r;
}

static int d_poll(struct pollfd *f, nfds_t n, int to)
{
    for (nfds_t i = 0; i < n; i++)
        f[i].events = ev_to_c(f[i].events);
    int r = poll(f, n, to);
    for (nfds_t i = 0; i < n; i++) {
        f[i].events = ev_to_d(f[i].events);
        f[i].revents = r >= 0 ? ev_to_d(f[i].revents) : 0;
    }
    return r;
}
DSYM("_poll", d_poll);
