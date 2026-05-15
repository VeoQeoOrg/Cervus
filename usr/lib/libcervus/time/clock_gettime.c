#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

typedef struct { int64_t tv_sec; int64_t tv_nsec; } __cervus_ts_raw_t;

int clock_gettime(int clk, struct timespec *tp)
{
    if (!tp) { __cervus_errno = EINVAL; return -1; }
    __cervus_ts_raw_t ts = {0, 0};
    long r = syscall2(SYS_CLOCK_GET, clk, &ts);
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    tp->tv_sec  = (time_t)ts.tv_sec;
    tp->tv_nsec = (long)ts.tv_nsec;
    return 0;
}
