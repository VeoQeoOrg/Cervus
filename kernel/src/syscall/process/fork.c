#include "../../../include/sched/ptrace.h"
#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/io/serial.h"

static int64_t fork_allowed(task_t *parent)
{
    uint64_t nproc = parent->rlim[RLIMIT_NPROC_K].cur;
    if (parent->uid != 0 && nproc != RLIM_INF_K && (uint64_t)task_count_uid(parent->uid) >= nproc)
        return -EAGAIN;
    return 0;
}

int64_t sys_fork(void)
{
    task_t *parent = syscall_cur_task();
    if (!parent) return -ESRCH;
    int64_t e = fork_allowed(parent);
    if (e < 0) return e;
    task_t *child = task_fork(parent);
    if (!child) return -ENOMEM;
    ptrace_note_child(parent, child, (child->flags & TASK_FLAG_VFORK) ? PTRACE_EVENT_VFORK : PTRACE_EVENT_FORK);
    return (int64_t)child->pid;
}

task_t *fork_process(task_t *parent, int share_vm, uintptr_t user_sp, int64_t *err)
{
    int64_t e = fork_allowed(parent);
    if (e < 0) { *err = e; return NULL; }
    task_t *child = task_fork_prepare(parent, share_vm, user_sp);
    if (!child) *err = -ENOMEM;
    return child;
}
