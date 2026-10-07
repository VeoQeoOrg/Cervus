#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

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
    __cervus_set_errno(EINVAL);
    return -1;
}

int sched_get_priority_max(int policy)
{
    if (policy == SCHED_FIFO || policy == SCHED_RR) return 99;
    if (policy == SCHED_OTHER || policy == SCHED_BATCH || policy == SCHED_IDLE) return 0;
    __cervus_set_errno(EINVAL);
    return -1;
}

static int sched_info(pid_t pid, int out[3])
{
    long r = syscall2(SYS_SCHED_GETSCHED, (uint64_t)pid, (uint64_t)out);
    if (r < 0) { __cervus_set_errno((int)-r); return -1; }
    return 0;
}

int sched_getscheduler(pid_t pid)
{
    int info[3];
    if (sched_info(pid, info) < 0) return -1;
    return info[0];
}

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
    if (!param || pid < 0) { __cervus_set_errno(EINVAL); return -1; }
    return (int)__cervus_sys_ret(syscall3(SYS_SCHED_SETSCHED, (uint64_t)pid, (uint64_t)policy,
                                          (uint64_t)param->sched_priority));
}

int sched_getparam(pid_t pid, struct sched_param *param)
{
    if (!param) { __cervus_set_errno(EINVAL); return -1; }
    int info[3];
    if (sched_info(pid, info) < 0) return -1;
    param->sched_priority = info[1];
    return 0;
}

int sched_setparam(pid_t pid, const struct sched_param *param)
{
    int policy = sched_getscheduler(pid);
    if (policy < 0) return -1;
    return sched_setscheduler(pid, policy, param);
}

int sched_rr_get_interval(pid_t pid, struct timespec *tp)
{
    if (!tp) { __cervus_set_errno(EINVAL); return -1; }
    int info[3];
    if (sched_info(pid, info) < 0) return -1;
    tp->tv_sec = info[2] / 1000;
    tp->tv_nsec = (long)(info[2] % 1000) * 1000000L;
    return 0;
}

int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *set)
{
    if (!set || size < sizeof(unsigned long)) { __cervus_set_errno(EINVAL); return -1; }
    long m = syscall1(SYS_SCHED_GETAFFINITY, (uint64_t)pid);
    if (m < 0) { __cervus_set_errno((int)-m); return -1; }
    unsigned char *b = (unsigned char *)set;
    for (size_t i = 0; i < size; i++) b[i] = 0;
    set->__bits[0] = (unsigned long)m;
    return 0;
}

int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *set)
{
    if (!set || size < sizeof(unsigned long)) { __cervus_set_errno(EINVAL); return -1; }
    return (int)__cervus_sys_ret(syscall2(SYS_SCHED_SETAFFINITY, (uint64_t)pid, (uint64_t)set->__bits[0]));
}

int sched_getcpu(void)
{
    return (int)__cervus_sys_ret(syscall0(SYS_GETCPU));
}
