#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"

int64_t sys_setgid(uint64_t g)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != UID_ROOT && !cap_has(t->capabilities, CAP_SETUID)) return -EPERM;
    if (g > 65535) return -EINVAL;
    t->gid = (uint32_t)g;
    return 0;
}
