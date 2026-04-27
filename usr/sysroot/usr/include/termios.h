#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <stdint.h>

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

#define BRKINT  0x0001
#define ICRNL   0x0002
#define INPCK   0x0004
#define ISTRIP  0x0008
#define IXON    0x0010

#define OPOST   0x0001

#define CS8     0x0030

#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define IEXTEN  0x0100

#define VMIN    6
#define VTIME   5
#define VINTR   0
#define VQUIT   1
#define VERASE  2
#define VKILL   3
#define VEOF    4

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#define TCGETS       0x5401
#define TCSETS       0x5402
#define TCSETSW      0x5403
#define TCSETSF      0x5404

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int optional_actions, const struct termios *t);

#endif