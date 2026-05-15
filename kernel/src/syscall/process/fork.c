#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/io/serial.h"

int64_t sys_fork(void)
{
    task_t *parent = syscall_cur_task();
    if (!parent) return -ESRCH;
    syscall_save_user_regs(parent);
    task_t *child = task_fork(parent);
    if (!child) return -ENOMEM;
    serial_printf("[SYSCALL] fork: parent pid=%u → child pid=%u\n",
                  parent->pid, child->pid);
    return (int64_t)child->pid;
}
