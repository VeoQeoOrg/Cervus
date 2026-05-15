#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"
#include "../../../include/io/serial.h"

int64_t sys_cap_drop(uint64_t mask)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    t->capabilities = cap_drop(t->capabilities, mask);
    serial_printf("[SYSCALL] cap_drop: pid=%u caps=0x%llx\n", t->pid, t->capabilities);
    return 0;
}
