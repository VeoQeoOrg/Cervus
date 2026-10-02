#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/mm.h"

int64_t sys_brk(uint64_t new_brk)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace || !t->mm) return -EINVAL;
    if (!new_brk) return (int64_t)t->mm->brk;
    return (int64_t)mm_set_brk(t->mm, new_brk);
}
