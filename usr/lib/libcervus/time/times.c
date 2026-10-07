#include <sys/times.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <time.h>
#include <sys/resource.h>

clock_t times(struct tms *buf)
{
    if (buf) {
        struct rusage self, kids;
        getrusage(RUSAGE_SELF, &self);
        getrusage(RUSAGE_CHILDREN, &kids);
        buf->tms_utime  = (clock_t)(self.ru_utime.tv_sec * 1000000L + self.ru_utime.tv_usec);
        buf->tms_stime  = (clock_t)(self.ru_stime.tv_sec * 1000000L + self.ru_stime.tv_usec);
        buf->tms_cutime = (clock_t)(kids.ru_utime.tv_sec * 1000000L + kids.ru_utime.tv_usec);
        buf->tms_cstime = (clock_t)(kids.ru_stime.tv_sec * 1000000L + kids.ru_stime.tv_usec);
    }
    return (clock_t)((uint64_t)syscall0(SYS_UPTIME) / 1000ULL);
}
