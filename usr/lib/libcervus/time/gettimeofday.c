#include <sys/time.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

typedef struct { int64_t tv_sec; int64_t tv_nsec; } __cervus_ts_raw_t;

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    (void)tz;
    if (!tv) { __cervus_errno = EINVAL; return -1; }
    __cervus_ts_raw_t ts = {0, 0};
    syscall2(SYS_CLOCK_GET, 0, &ts);
    tv->tv_sec  = (time_t)ts.tv_sec;
    tv->tv_usec = (long)(ts.tv_nsec / 1000);
    return 0;
}
