#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    (void)tz;
    if (!tv || tv->tv_usec < 0 || tv->tv_usec >= 1000000) { __cervus_set_errno(EINVAL); return -1; }
    return (int)__cervus_sys_ret(syscall2(SYS_CLOCK_SET, (uint64_t)tv->tv_sec, (uint64_t)tv->tv_usec * 1000));
}

int clock_settime(clockid_t clk, const struct timespec *ts)
{
    if (clk != CLOCK_REALTIME) { __cervus_set_errno(EINVAL); return -1; }
    if (!ts || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) { __cervus_set_errno(EINVAL); return -1; }
    return (int)__cervus_sys_ret(syscall2(SYS_CLOCK_SET, (uint64_t)ts->tv_sec, (uint64_t)ts->tv_nsec));
}
