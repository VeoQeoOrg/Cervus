#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/sched.h"

static task_t *resolve_target(uint32_t pid) {
    if (pid == 0) return syscall_cur_task();
    return task_find_by_pid(pid);
}

int64_t sys_getpgid(uint64_t pid)
{
    task_t *t = resolve_target((uint32_t)pid);
    if (!t) return -ESRCH;
    return (int64_t)t->pgid;
}

int64_t sys_setpgid(uint64_t pid, uint64_t pgid)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    task_t *t = resolve_target((uint32_t)pid);
    if (!t) return -ESRCH;

    uint32_t new_pgid = (uint32_t)pgid;
    if (new_pgid == 0) new_pgid = t->pid;
    t->pgid = new_pgid;
    return 0;
}

int64_t sys_getsid(uint64_t pid)
{
    task_t *t = resolve_target((uint32_t)pid);
    if (!t) return -ESRCH;
    return (int64_t)t->sid;
}

int64_t sys_setsid(void)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    me->sid  = me->pid;
    me->pgid = me->pid;
    return (int64_t)me->sid;
}
