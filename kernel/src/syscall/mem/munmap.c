#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/vmm.h"

int64_t sys_munmap(uint64_t addr, uint64_t length)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace || addr & 0xFFF || !length) return -EINVAL;
    size_t pages = (length + 0xFFFULL) >> 12;
    for (size_t i = 0; i < pages; i++) vmm_unmap_page(t->pagemap, addr + i * 0x1000);
    return 0;
}
