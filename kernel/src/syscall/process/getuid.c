#include "../../../include/syscall/syscall_internal.h"

int64_t sys_getuid(void)
{
    task_t *t = syscall_cur_task();
    return t ? (int64_t)t->uid : -ESRCH;
}
