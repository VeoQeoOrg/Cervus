#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/syscall.h>

#define _IOC_NRBITS   8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS  2

#define _IOC_NRSHIFT   0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT  (_IOC_SIZESHIFT + _IOC_SIZEBITS)

#define _IOC_NONE  0U
#define _IOC_WRITE 1U
#define _IOC_READ  2U

#define _IOC(dir, type, nr, size)                                         \
    (((unsigned long)(dir) << _IOC_DIRSHIFT) |                            \
     ((unsigned long)(type) << _IOC_TYPESHIFT) |                          \
     ((unsigned long)(nr) << _IOC_NRSHIFT) |                              \
     ((unsigned long)(size) << _IOC_SIZESHIFT))

#define _IO(type, nr)             _IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, argtype)   _IOC(_IOC_READ, (type), (nr), sizeof(argtype))
#define _IOW(type, nr, argtype)   _IOC(_IOC_WRITE, (type), (nr), sizeof(argtype))
#define _IOWR(type, nr, argtype)  _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(argtype))

#define _IOC_DIR(cmd)  (((cmd) >> _IOC_DIRSHIFT) & ((1U << _IOC_DIRBITS) - 1))
#define _IOC_TYPE(cmd) (((cmd) >> _IOC_TYPESHIFT) & ((1U << _IOC_TYPEBITS) - 1))
#define _IOC_NR(cmd)   (((cmd) >> _IOC_NRSHIFT) & ((1U << _IOC_NRBITS) - 1))
#define _IOC_SIZE(cmd) (((cmd) >> _IOC_SIZESHIFT) & ((1U << _IOC_SIZEBITS) - 1))

#define TIOCGWINSZ   0x5413
#define TIOCSWINSZ   0x5414
#define TIOCGCURSOR  0x5480
#define TIOCSNONBLOCK 0x5481
#define TIOCGPGRP    0x540F
#define TIOCSPGRP    0x5410
#define TIOCGPTN     0x80045430
#define TIOCSPTLCK   0x40045431

#define FIOCLEX      0x5451
#define FIONCLEX     0x5450
#define FIONREAD     0x541B
#define FIONBIO      0x5421
#define FIOASYNC     0x5452

#define SIOCSTTL     0x5460

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

struct cursor_pos {
    uint32_t row;
    uint32_t col;
};

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif
#endif
