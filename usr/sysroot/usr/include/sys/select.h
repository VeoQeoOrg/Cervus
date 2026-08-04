#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include <sys/time.h>

#define FD_SETSIZE 1024

typedef struct {
    unsigned char fds_bits[FD_SETSIZE / 8];
} fd_set;

#define FD_ZERO(s)     do { for (int _i = 0; _i < FD_SETSIZE / 8; _i++) (s)->fds_bits[_i] = 0; } while (0)
#define FD_SET(fd, s)   ((s)->fds_bits[(fd) / 8] |=  (unsigned char)(1 << ((fd) & 7)))
#define FD_CLR(fd, s)   ((s)->fds_bits[(fd) / 8] &= (unsigned char)~(1 << ((fd) & 7)))
#define FD_ISSET(fd, s) (((s)->fds_bits[(fd) / 8] >> ((fd) & 7)) & 1)

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#endif
