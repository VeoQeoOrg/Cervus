#ifndef _SCHED_H
#define _SCHED_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <time.h>

#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2
#define SCHED_BATCH 3
#define SCHED_IDLE  5

struct sched_param {
    int sched_priority;
};

#define CPU_SETSIZE 1024
typedef struct {
    unsigned long __bits[CPU_SETSIZE / (8 * sizeof(unsigned long))];
} cpu_set_t;

#define __CPU_WORD(c) ((c) / (8 * sizeof(unsigned long)))
#define __CPU_BIT(c)  (1UL << ((c) % (8 * sizeof(unsigned long))))
#define CPU_ZERO(s)       __builtin_memset((s), 0, sizeof(cpu_set_t))
#define CPU_SET(c, s)     ((c) < CPU_SETSIZE ? ((s)->__bits[__CPU_WORD(c)] |= __CPU_BIT(c)) : 0)
#define CPU_CLR(c, s)     ((c) < CPU_SETSIZE ? ((s)->__bits[__CPU_WORD(c)] &= ~__CPU_BIT(c)) : 0)
#define CPU_ISSET(c, s)   ((c) < CPU_SETSIZE && ((s)->__bits[__CPU_WORD(c)] & __CPU_BIT(c)) != 0)
#define CPU_COUNT(s)      __cervus_cpu_count(s)
int __cervus_cpu_count(const cpu_set_t *s);

int sched_yield(void);
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);
int sched_getscheduler(pid_t pid);
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param);
int sched_getparam(pid_t pid, struct sched_param *param);
int sched_setparam(pid_t pid, const struct sched_param *param);
int sched_rr_get_interval(pid_t pid, struct timespec *tp);
int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *set);
int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *set);
int sched_getcpu(void);

#ifdef __cplusplus
}
#endif
#endif
