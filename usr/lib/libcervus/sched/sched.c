#include <sched.h>
#include <errno.h>
#include <unistd.h>

int __cervus_cpu_count(const cpu_set_t *s)
{
    int n = 0;
    for (size_t i = 0; i < sizeof s->__bits / sizeof s->__bits[0]; i++)
        for (unsigned long w = s->__bits[i]; w; w &= w - 1) n++;
    return n;
}

int sched_get_priority_min(int policy)
{
    if (policy == SCHED_FIFO || policy == SCHED_RR) return 1;
    if (policy == SCHED_OTHER || policy == SCHED_BATCH || policy == SCHED_IDLE) return 0;
    errno = EINVAL;
    return -1;
}

int sched_get_priority_max(int policy)
{
    if (policy == SCHED_FIFO || policy == SCHED_RR) return 99;
    if (policy == SCHED_OTHER || policy == SCHED_BATCH || policy == SCHED_IDLE) return 0;
    errno = EINVAL;
    return -1;
}

int sched_getscheduler(pid_t pid)
{
    (void)pid;
    return SCHED_OTHER;
}

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
    (void)pid;
    if (!param) { errno = EINVAL; return -1; }
    if (policy != SCHED_OTHER && policy != SCHED_BATCH && policy != SCHED_IDLE) { errno = EPERM; return -1; }
    if (param->sched_priority != 0) { errno = EINVAL; return -1; }
    return 0;
}

int sched_getparam(pid_t pid, struct sched_param *param)
{
    (void)pid;
    if (!param) { errno = EINVAL; return -1; }
    param->sched_priority = 0;
    return 0;
}

int sched_setparam(pid_t pid, const struct sched_param *param)
{
    return sched_setscheduler(pid, SCHED_OTHER, param);
}

int sched_rr_get_interval(pid_t pid, struct timespec *tp)
{
    (void)pid;
    if (!tp) { errno = EINVAL; return -1; }
    tp->tv_sec = 0;
    tp->tv_nsec = 10000000;
    return 0;
}

int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *set)
{
    (void)pid;
    if (!set || size < sizeof(unsigned long)) { errno = EINVAL; return -1; }
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    unsigned char *b = (unsigned char *)set;
    for (size_t i = 0; i < size; i++) b[i] = 0;
    for (long c = 0; c < n && (size_t)c < size * 8; c++) b[c / 8] |= (unsigned char)(1u << (c % 8));
    return 0;
}

int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *set)
{
    (void)pid;
    (void)size;
    if (!set) { errno = EINVAL; return -1; }
    return 0;
}

int sched_getcpu(void)
{
    return 0;
}
