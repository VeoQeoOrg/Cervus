#include <sys/times.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <time.h>

clock_t times(struct tms *buf)
{
    cervus_task_info_t info;
    clock_t used = 0;

    if (syscall2(SYS_TASK_INFO, 0, (unsigned long)&info) >= 0)
        used = (clock_t)(info.total_runtime_ns / 1000ULL);

    if (buf) {
        buf->tms_utime  = used;
        buf->tms_stime  = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return (clock_t)((uint64_t)syscall0(SYS_UPTIME) / 1000ULL);
}
