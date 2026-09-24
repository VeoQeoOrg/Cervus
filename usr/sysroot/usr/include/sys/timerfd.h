#ifndef _SYS_TIMERFD_H
#define _SYS_TIMERFD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

#define TFD_CLOEXEC  0x80000
#define TFD_TIMER_ABSTIME 1
#define TFD_NONBLOCK 0x800

#define CLOCK_MONOTONIC_TFD 1

int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
                    struct itimerspec *old_value);
int timerfd_gettime(int fd, struct itimerspec *curr_value);

#ifdef __cplusplus
}
#endif
#endif
