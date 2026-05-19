#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <stdint.h>
#include <sys/types.h>

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
};

#define IGNBRK  0x00000001u
#define BRKINT  0x00000002u
#define IGNPAR  0x00000004u
#define PARMRK  0x00000008u
#define INPCK   0x00000010u
#define ISTRIP  0x00000020u
#define INLCR   0x00000040u
#define IGNCR   0x00000080u
#define ICRNL   0x00000100u
#define IXON    0x00000200u
#define IXOFF   0x00000400u
#define IXANY   0x00000800u
#define IMAXBEL 0x00001000u
#define IUTF8   0x00002000u

#define OPOST   0x00000001u
#define ONLCR   0x00000004u
#define OCRNL   0x00000008u
#define ONOCR   0x00000010u
#define ONLRET  0x00000020u
#define OFILL   0x00000040u
#define OFDEL   0x00000080u

#define CSIZE   0x00000030u
#define CS5     0x00000000u
#define CS6     0x00000010u
#define CS7     0x00000020u
#define CS8     0x00000030u
#define CSTOPB  0x00000040u
#define CREAD   0x00000080u
#define PARENB  0x00000100u
#define PARODD  0x00000200u
#define HUPCL   0x00000400u
#define CLOCAL  0x00000800u
#define CRTSCTS 0x80000000u

#define CBAUD       0x0000F000u
#define CBAUD_SHIFT 12

#define ISIG    0x00000001u
#define ICANON  0x00000002u
#define ECHO    0x00000008u
#define ECHOE   0x00000010u
#define ECHOK   0x00000020u
#define ECHONL  0x00000040u
#define NOFLSH  0x00000080u
#define IEXTEN  0x00000100u
#define TOSTOP  0x00000200u
#define ECHOCTL 0x00000400u
#define ECHOKE  0x00000800u
#define ECHOPRT 0x00001000u

#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

#define B0       0
#define B50      1
#define B75      2
#define B110     3
#define B134     4
#define B150     5
#define B200     6
#define B300     7
#define B600     8
#define B1200    9
#define B1800    10
#define B2400    11
#define B4800    12
#define B9600    13
#define B19200   14
#define B38400   15

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

#define TCOOFF    0
#define TCOON     1
#define TCIOFF    2
#define TCION     3

#define TCGETS       0x5401
#define TCSETS       0x5402
#define TCSETSW      0x5403
#define TCSETSF      0x5404
#define TCFLSH       0x540B
#define TCDRAIN      0x540C
#define TCSBRK       0x540D
#define TCXONC       0x540E

int     tcgetattr(int fd, struct termios *t);
int     tcsetattr(int fd, int optional_actions, const struct termios *t);
int     tcflush(int fd, int queue);
int     tcdrain(int fd);
int     tcflow(int fd, int action);
int     tcsendbreak(int fd, int duration);
pid_t   tcgetsid(int fd);

void    cfmakeraw(struct termios *t);
speed_t cfgetispeed(const struct termios *t);
speed_t cfgetospeed(const struct termios *t);
int     cfsetispeed(struct termios *t, speed_t s);
int     cfsetospeed(struct termios *t, speed_t s);
int     cfsetspeed(struct termios *t, speed_t s);

#endif
