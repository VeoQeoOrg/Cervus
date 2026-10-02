#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/mm.h"

int64_t sys_munmap(uint64_t addr, uint64_t length)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace || !t->mm || (addr & 0xFFF) || !length) return -EINVAL;
    return mm_unmap(t->mm, addr, length);
}
