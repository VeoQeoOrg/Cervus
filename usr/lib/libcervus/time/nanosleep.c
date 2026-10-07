#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        __cervus_set_errno(EINVAL);
        return -1;
    }
    uint64_t ns = (uint64_t)req->tv_sec * 1000000000ULL + (uint64_t)req->tv_nsec;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int cp = __cervus_cp_enter();
    long r = syscall1(SYS_SLEEP_NS, ns);
    __cervus_cp_leave(cp);
    if (r < 0 && r > -4096) {
        if (rem) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            uint64_t spent = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
            uint64_t left = spent < ns ? ns - spent : 0;
            rem->tv_sec = (time_t)(left / 1000000000ULL);
            rem->tv_nsec = (long)(left % 1000000000ULL);
        }
        __cervus_set_errno((int)-r);
        return -1;
    }
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}
