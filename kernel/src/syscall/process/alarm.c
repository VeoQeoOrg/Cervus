#include "../../../include/syscall/syscall_internal.h"

int64_t sys_alarm(uint64_t seconds)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    return (int64_t)task_set_alarm(t, seconds);
}
