#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"

int64_t sys_setuid(uint64_t u)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != UID_ROOT && !cap_has(t->capabilities, CAP_SETUID)) return -EPERM;
    if (u > 65535) return -EINVAL;
    t->uid = (uint32_t)u;
    return 0;
}
