#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"

int64_t sys_brk(uint64_t new_brk)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace) return -EINVAL;
    if (!new_brk) return (int64_t)t->brk_current;
    if (new_brk < t->brk_start || new_brk > t->brk_max) return (int64_t)t->brk_current;

    uintptr_t old_brk  = t->brk_current;
    uintptr_t old_page = (old_brk + 0xFFFULL) & ~0xFFFULL;
    uintptr_t new_page = (new_brk + 0xFFFULL) & ~0xFFFULL;

    if (new_brk > old_brk) {
        for (uintptr_t p = old_page; p < new_page; p += 0x1000) {
            void *ph = pmm_alloc_zero(1);
            if (!ph) return (int64_t)t->brk_current;
            if (!vmm_map_page(t->pagemap, p, pmm_virt_to_phys(ph),
                              VMM_PRESENT | VMM_WRITE | VMM_USER | VMM_NOEXEC))
                { pmm_free(ph, 1); return (int64_t)t->brk_current; }
        }
    } else {
        for (uintptr_t p = new_page; p < old_page; p += 0x1000)
            vmm_unmap_page(t->pagemap, p);
    }
    t->brk_current = new_brk;
    return (int64_t)new_brk;
}
