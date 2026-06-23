#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/io/serial.h"

int64_t sys_fork(void)
{
    task_t *parent = syscall_cur_task();
    if (!parent) return -ESRCH;
    task_t *child = task_fork(parent);
    if (!child) return -ENOMEM;
    return (int64_t)child->pid;
}
