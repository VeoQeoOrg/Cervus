#include <sys/time.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    (void)tz;
    if (!tv) { __cervus_errno = EINVAL; return -1; }
    return (int)__cervus_sys_ret(syscall1(SYS_CLOCK_SET, (uint64_t)tv->tv_sec));
}
