#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/smp/smp.h"
#include "../../../include/smp/percpu.h"
#include <string.h>

#define PRIO_PROCESS_K 0
#define PRIO_PGRP_K    1
#define PRIO_USER_K    2

extern int task_collect_pids(uint32_t *out, int max);

static bool privileged(task_t *t)
{
    return t->uid == 0 || cap_has(t->capabilities, CAP_SET_PRIO);
}

static bool prio_match(task_t *me, task_t *o, int which, uint32_t who)
{
    if (!o->is_userspace || o->state == TASK_ZOMBIE || o->state == TASK_DEAD) return false;
    switch (which) {
    case PRIO_PROCESS_K: return o->pid == (who ? who : me->pid);
    case PRIO_PGRP_K:    return o->pgid == (who ? who : me->pgid);
    case PRIO_USER_K:    return o->uid == (who ? who : me->uid);
    }
    return false;
}

static int collect(task_t *me, int which, uint32_t who, task_t **out, int max)
{
    static uint32_t pids[1024];
    static spinlock_t lock = SPINLOCK_INIT;
    uint64_t f = spinlock_acquire_irqsave(&lock);
    int n = task_collect_pids(pids, 1024), k = 0;
    for (int i = 0; i < n && k < max; i++) {
        task_t *o = task_find_by_pid(pids[i]);
        if (o && prio_match(me, o, which, who)) out[k++] = o;
    }
    spinlock_release_irqrestore(&lock, f);
    return k;
}

int64_t sys_getpriority(uint64_t which, uint64_t who)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    if (which > PRIO_USER_K) return -EINVAL;
    task_t *list[256];
    int n = collect(me, (int)which, (uint32_t)who, list, 256);
    if (!n) return -ESRCH;
    int best = 19;
    for (int i = 0; i < n; i++)
        if (list[i]->nice < best) best = list[i]->nice;
    return 20 - best;
}

int64_t sys_setpriority(uint64_t which, uint64_t who, uint64_t prio_a)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    if (which > PRIO_USER_K) return -EINVAL;
    int prio = (int)(int64_t)prio_a;
    if (prio < -20) prio = -20;
    if (prio > 19) prio = 19;
    task_t *list[256];
    int n = collect(me, (int)which, (uint32_t)who, list, 256);
    if (!n) return -ESRCH;
    int64_t err = 0;
    for (int i = 0; i < n; i++) {
        task_t *o = list[i];
        if (!privileged(me) && o->uid != me->uid) { err = -EPERM; continue; }
        if (prio < o->nice && !privileged(me)) {
            uint64_t lim = me->rlim[RLIMIT_NICE_K].cur;
            if (lim != RLIM_INF_K && (uint64_t)(20 - prio) > lim) { err = -EACCES; continue; }
        }
        o->nice = prio;
        task_apply_nice(o);
    }
    return err;
}

int64_t sys_prlimit(uint64_t pid, uint64_t res_a, uint64_t new_ptr, uint64_t old_ptr)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    int res = (int)res_a;
    if (res < 0 || res >= RLIMIT_COUNT) return -EINVAL;
    task_t *t = pid ? task_find_by_pid((uint32_t)pid) : me;
    if (!t || !t->is_userspace) return -ESRCH;
    if (t != me && me->uid != 0 && me->uid != t->uid) return -EPERM;

    struct { uint64_t cur, max; } nv;
    if (new_ptr) {
        if (syscall_copy_from_user(&nv, (const void *)new_ptr, sizeof(nv)) < 0) return -EFAULT;
        if (nv.cur > nv.max) return -EINVAL;
        if (nv.max > t->rlim[res].max && me->uid != 0) return -EPERM;
        if (res == RLIMIT_NOFILE_K && (nv.cur > TASK_MAX_FDS || nv.max > TASK_MAX_FDS)) {
            if (nv.max > TASK_MAX_FDS && me->uid != 0) return -EPERM;
            if (nv.cur > TASK_MAX_FDS) nv.cur = TASK_MAX_FDS;
            if (nv.max > TASK_MAX_FDS) nv.max = TASK_MAX_FDS;
        }
    }
    if (old_ptr) {
        struct { uint64_t cur, max; } ov = { t->rlim[res].cur, t->rlim[res].max };
        if (syscall_copy_to_user((void *)old_ptr, &ov, sizeof(ov)) < 0) return -EFAULT;
    }
    if (new_ptr) {
        t->rlim[res].cur = nv.cur;
        t->rlim[res].max = nv.max;
        if (res == RLIMIT_CPU_K) t->xcpu_sent = 0;
    }
    return 0;
}

int64_t sys_sched_setaffinity(uint64_t pid, uint64_t mask)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    task_t *t = pid ? task_find_by_pid((uint32_t)pid) : me;
    if (!t || !t->is_userspace) return -ESRCH;
    if (t != me && me->uid != 0 && me->uid != t->uid) return -EPERM;
    uint32_t n = smp_get_cpu_count();
    uint64_t online = n >= 64 ? ~0ULL : ((1ULL << n) - 1);
    mask &= online;
    if (!mask) return -EINVAL;
    t->cpu_affinity = mask == online ? 0 : mask;
    if (t == me && !(mask & (1ULL << smp_cpu_index()))) {
        percpu_t *pc = get_percpu();
        if (pc) pc->need_resched = true;
    }
    return 0;
}

int64_t sys_sched_getaffinity(uint64_t pid)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    task_t *t = pid ? task_find_by_pid((uint32_t)pid) : me;
    if (!t || !t->is_userspace) return -ESRCH;
    uint32_t n = smp_get_cpu_count();
    uint64_t online = n >= 64 ? ~0ULL : ((1ULL << n) - 1);
    uint64_t m = t->cpu_affinity ? (t->cpu_affinity & online) : online;
    return (int64_t)(m & 0x7FFFFFFFFFFFFFFFULL);
}

int64_t sys_getcpu(void)
{
    return (int64_t)smp_cpu_index();
}

int64_t sys_sched_setscheduler(uint64_t pid, uint64_t policy_a, uint64_t prio_a)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    task_t *t = pid ? task_find_by_pid((uint32_t)pid) : me;
    if (!t || !t->is_userspace) return -ESRCH;
    if (t != me && me->uid != 0 && me->uid != t->uid) return -EPERM;
    int policy = (int)policy_a;
    int prio = (int)prio_a;
    bool rt = policy == SCHED_FIFO_K || policy == SCHED_RR_K;
    if (policy != SCHED_OTHER_K && policy != SCHED_BATCH_K && policy != SCHED_IDLE_K && !rt) return -EINVAL;
    if (rt ? (prio < 1 || prio > 99) : prio != 0) return -EINVAL;
    if (rt && !privileged(me)) {
        uint64_t lim = me->rlim[RLIMIT_RTPRIO_K].cur;
        if (lim == 0 || (lim != RLIM_INF_K && (uint64_t)prio > lim)) return -EPERM;
    }
    t->policy = (uint8_t)policy;
    t->rt_prio = (uint8_t)prio;
    task_apply_policy(t);
    if (policy == SCHED_FIFO_K || policy == SCHED_RR_K) t->time_slice = t->time_slice_init;
    return 0;
}

int64_t sys_sched_getscheduler(uint64_t pid, uint64_t out_ptr)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    task_t *t = pid ? task_find_by_pid((uint32_t)pid) : me;
    if (!t || !t->is_userspace) return -ESRCH;
    int32_t out[3] = { t->policy, t->rt_prio, t->policy == SCHED_FIFO_K ? 0 : t->time_slice_init };
    if (out_ptr && syscall_copy_to_user((void *)out_ptr, out, sizeof(out)) < 0) return -EFAULT;
    return t->policy;
}
