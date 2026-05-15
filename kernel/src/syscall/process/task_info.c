#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"
#include <string.h>

int64_t sys_task_info(uint64_t pid_arg, uint64_t buf_ptr)
{
    if (!buf_ptr) return -EINVAL;
    task_t *target = (pid_arg == 0) ? syscall_cur_task() : task_find_by_pid((uint32_t)pid_arg);
    if (!target) return -ESRCH;
    task_t *me = syscall_cur_task();
    if (me && me != target && !cap_has(me->capabilities, CAP_TASK_INFO)) return -EPERM;

    cervus_task_info_t info;
    memset(&info, 0, sizeof(info));
    info.pid              = target->pid;
    info.ppid             = target->ppid;
    info.uid              = target->uid;
    info.gid              = target->gid;
    info.capabilities     = target->capabilities;
    info.state            = (uint32_t)target->state;
    info.priority         = (uint32_t)target->priority;
    info.total_runtime_ns = target->total_runtime;
    strncpy(info.name, target->name, sizeof(info.name) - 1);
    return syscall_copy_to_user((void *)buf_ptr, &info, sizeof(info));
}
