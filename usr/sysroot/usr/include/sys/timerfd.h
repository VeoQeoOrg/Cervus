#ifndef _SYS_TIMERFD_H
#define _SYS_TIMERFD_H

#include <time.h>

#define TFD_NONBLOCK 0x800

#define CLOCK_MONOTONIC_TFD 1

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
                    struct itimerspec *old_value);

#endif
