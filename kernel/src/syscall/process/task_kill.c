#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"

int64_t sys_task_kill(uint64_t pid_arg)
{
    task_t *me = syscall_cur_task();
    task_t *target = task_find_by_pid((uint32_t)pid_arg);
    if (!target) return -ESRCH;
    extern int signal_may_send(task_t *me, task_t *t);
    bool own = (target->ppid == (me ? me->pid : 0));
    if (!own && !signal_may_send(me, target)) return -EPERM;
    task_kill_subtree(target);
    return 0;
}
