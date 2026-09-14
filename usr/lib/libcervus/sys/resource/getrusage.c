#include <sys/resource.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <string.h>
#include <errno.h>

int getrusage(int who, struct rusage *usage)
{
    if (!usage) { errno = EFAULT; return -1; }
    memset(usage, 0, sizeof *usage);
    if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN) { errno = EINVAL; return -1; }
    if (who == RUSAGE_CHILDREN) return 0;

    cervus_task_info_t info;
    if (syscall2(SYS_TASK_INFO, 0, (unsigned long)&info) < 0) { errno = EINVAL; return -1; }

    usage->ru_utime.tv_sec  = (time_t)(info.total_runtime_ns / 1000000000ULL);
    usage->ru_utime.tv_usec = (long)((info.total_runtime_ns % 1000000000ULL) / 1000ULL);
    usage->ru_maxrss        = (long)(info.rss_bytes / 1024ULL);
    return 0;
}
