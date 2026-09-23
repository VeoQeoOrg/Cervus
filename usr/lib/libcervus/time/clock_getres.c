#include <time.h>
#include <errno.h>
#include <libcervus.h>

int clock_getres(clockid_t clk, struct timespec *res)
{
    struct timespec probe;
    if (clock_gettime(clk, &probe) < 0) return -1;
    if (res) {
        res->tv_sec = 0;
        res->tv_nsec = 1;
    }
    return 0;
}
