#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/mm.h"
#include <stdlib.h>

int64_t sys_mremap(uint64_t old, uint64_t old_len, uint64_t new_len, uint64_t flags, uint64_t new_addr)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace || !t->mm) return -EINVAL;
    return mm_remap(t->mm, old, old_len, new_len, (int)flags, new_addr);
}

int64_t sys_mincore(uint64_t addr, uint64_t len, uint64_t vec)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace || !t->mm) return -EINVAL;
    if (addr & 0xFFF) return -EINVAL;
    uint64_t pages = (len + 0xFFF) >> 12;
    if (pages && !syscall_uptr_validate_write((void *)vec, pages)) return -EFAULT;
    uint8_t buf[512];
    for (uint64_t done = 0; done < pages;) {
        uint64_t n = pages - done < sizeof buf ? pages - done : sizeof buf;
        int r = mm_mincore(t->mm, addr + done * 4096, n * 4096, buf);
        if (r < 0) return r;
        if (syscall_copy_to_user((void *)(vec + done), buf, n) < 0) return -EFAULT;
        done += n;
    }
    return 0;
}
