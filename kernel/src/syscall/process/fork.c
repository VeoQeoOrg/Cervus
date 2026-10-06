#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/io/serial.h"

int64_t sys_fork(void)
{
    task_t *parent = syscall_cur_task();
    if (!parent) return -ESRCH;
    uint64_t nproc = parent->rlim[RLIMIT_NPROC_K].cur;
    if (parent->uid != 0 && nproc != RLIM_INF_K && (uint64_t)task_count_uid(parent->uid) >= nproc)
        return -EAGAIN;
    task_t *child = task_fork(parent);
    if (!child) return -ENOMEM;
    return (int64_t)child->pid;
}
