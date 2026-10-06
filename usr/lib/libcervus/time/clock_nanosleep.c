#include <time.h>
#include <errno.h>

int clock_nanosleep(clockid_t clk, int flags, const struct timespec *req, struct timespec *rem)
{
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) return EINVAL;
    if (clk == CLOCK_PROCESS_CPUTIME_ID || clk == CLOCK_THREAD_CPUTIME_ID) return ENOTSUP;
    if (clk != CLOCK_REALTIME && clk != CLOCK_MONOTONIC && clk != CLOCK_BOOTTIME) return EINVAL;

    struct timespec rel = *req;
    if (flags & TIMER_ABSTIME) {
        struct timespec now;
        if (clock_gettime(clk, &now) < 0) return errno;
        rel.tv_sec = req->tv_sec - now.tv_sec;
        rel.tv_nsec = req->tv_nsec - now.tv_nsec;
        if (rel.tv_nsec < 0) { rel.tv_nsec += 1000000000L; rel.tv_sec--; }
        if (rel.tv_sec < 0) return 0;
        rem = NULL;
    }
    int saved = errno;
    if (nanosleep(&rel, rem) == 0) return 0;
    int e = errno;
    errno = saved;
    return e;
}
