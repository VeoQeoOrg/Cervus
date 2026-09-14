#include "../../../include/syscall/syscall_internal.h"

int64_t sys_umask(uint64_t mask)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    uint32_t old = t->umask & 0777u;
    t->umask = (uint32_t)(mask & 0777u);
    return (int64_t)old;
}
