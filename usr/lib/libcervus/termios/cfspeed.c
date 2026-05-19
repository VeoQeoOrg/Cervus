#include <termios.h>
#include <errno.h>
#include <libcervus.h>

speed_t cfgetispeed(const struct termios *t)
{
    if (!t) return 0;
    return (speed_t)((t->c_cflag & CBAUD) >> CBAUD_SHIFT);
}

speed_t cfgetospeed(const struct termios *t)
{
    if (!t) return 0;
    return (speed_t)((t->c_cflag & CBAUD) >> CBAUD_SHIFT);
}

static int valid_speed(speed_t s)
{
    return s <= B38400;
}

int cfsetispeed(struct termios *t, speed_t s)
{
    if (!t || !valid_speed(s)) { __cervus_errno = EINVAL; return -1; }
    t->c_cflag = (t->c_cflag & ~CBAUD) | ((s << CBAUD_SHIFT) & CBAUD);
    return 0;
}

int cfsetospeed(struct termios *t, speed_t s)
{
    return cfsetispeed(t, s);
}

int cfsetspeed(struct termios *t, speed_t s)
{
    return cfsetispeed(t, s);
}
