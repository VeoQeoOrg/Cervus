#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req) { __cervus_errno = EINVAL; return -1; }
    uint64_t ns = (uint64_t)req->tv_sec * 1000000000ULL + (uint64_t)req->tv_nsec;
    long r = syscall1(SYS_SLEEP_NS, ns);
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    return 0;
}
