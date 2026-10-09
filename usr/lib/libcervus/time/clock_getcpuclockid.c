#include <time.h>
#include <errno.h>

int clock_getcpuclockid(pid_t pid, clockid_t *clk)
{
    clockid_t id = (clockid_t)((-pid - 1) * 8U + 2);
    struct timespec ts;
    if (clock_gettime(id, &ts) < 0) return errno == EINVAL ? ESRCH : errno;
    *clk = id;
    return 0;
}
