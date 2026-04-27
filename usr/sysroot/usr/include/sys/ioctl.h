#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <stdint.h>
#include <sys/syscall.h>

#define TIOCGWINSZ   0x5413
#define TIOCSWINSZ   0x5414
#define TIOCGPGRP    0x540F
#define TIOCSPGRP    0x5410
#define TIOCGPTN     0x80045430
#define TIOCSPTLCK   0x40045431

#define FIOCLEX      0x5451
#define FIONCLEX     0x5450
#define FIONREAD     0x541B
#define FIONBIO      0x5421
#define FIOASYNC     0x5452

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

int ioctl(int fd, unsigned long request, ...);

#endif