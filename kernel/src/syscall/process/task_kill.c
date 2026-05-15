#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"

int64_t sys_task_kill(uint64_t pid_arg)
{
    task_t *me = syscall_cur_task();
    task_t *target = task_find_by_pid((uint32_t)pid_arg);
    if (!target) return -ESRCH;
    bool own = (target->ppid == (me ? me->pid : 0));
    if (!own && !cap_has(me ? me->capabilities : 0, CAP_KILL_ANY)) return -EPERM;
    task_kill(target);
    return 0;
}
