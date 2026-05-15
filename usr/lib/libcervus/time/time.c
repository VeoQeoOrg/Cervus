#include <time.h>
#include <stdint.h>
#include <sys/syscall.h>

typedef struct { int64_t tv_sec; int64_t tv_nsec; } __cervus_ts_raw_t;

time_t time(time_t *t)
{
    __cervus_ts_raw_t ts = {0, 0};
    syscall2(SYS_CLOCK_GET, 0, &ts);
    time_t v = (time_t)ts.tv_sec;
    if (t) *t = v;
    return v;
}
