#ifndef _KERNEL_POLL_H
#define _KERNEL_POLL_H

#define POLLIN     0x001
#define POLLPRI    0x002
#define POLLOUT    0x004
#define POLLERR    0x008
#define POLLHUP    0x010
#define POLLNVAL   0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#define POLLWRNORM 0x100
#define POLLWRBAND 0x200
#define POLLMSG    0x400
#define POLLRDHUP  0x2000

static inline int poll_expand(int rev) {
    if (rev & POLLIN)  rev |= POLLRDNORM;
    if (rev & POLLOUT) rev |= POLLWRNORM;
    return rev;
}

#endif
