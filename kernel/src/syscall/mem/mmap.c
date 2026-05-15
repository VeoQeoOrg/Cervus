#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/io/serial.h"

int64_t sys_mmap(uint64_t hint, uint64_t length, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset)
{
    (void)offset;
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace) return (int64_t)MAP_FAILED;
    if (!(flags & MAP_ANONYMOUS)) return (int64_t)MAP_FAILED;
    if (fd != (uint64_t)-1 && fd != 0) return (int64_t)MAP_FAILED;
    if (!length) return (int64_t)MAP_FAILED;

    size_t pages = (length + 0xFFFULL) >> 12;
    uintptr_t addr;
    if (flags & MAP_FIXED)       addr = hint & ~0xFFFULL;
    else if (hint)               addr = hint & ~0xFFFULL;
    else { addr = (t->brk_max - (uint64_t)pages * 0x1000) & ~0xFFFULL; t->brk_max = addr; }

    uint64_t vf = VMM_PRESENT | VMM_USER;
    if (prot & PROT_WRITE) vf |= VMM_WRITE;
    if (!(prot & PROT_EXEC)) vf |= VMM_NOEXEC;

    for (size_t i = 0; i < pages; i++) {
        void *ph = pmm_alloc_zero(1);
        if (!ph) {
            for (size_t j = 0; j < i; j++) vmm_unmap_page(t->pagemap, addr + j * 0x1000);
            return (int64_t)MAP_FAILED;
        }
        if (!vmm_map_page(t->pagemap, addr + i * 0x1000, pmm_virt_to_phys(ph), vf)) {
            pmm_free(ph, 1);
            for (size_t j = 0; j < i; j++) vmm_unmap_page(t->pagemap, addr + j * 0x1000);
            return (int64_t)MAP_FAILED;
        }
    }
    serial_printf("[SYSCALL] mmap: addr=0x%llx pages=%zu prot=0x%llx\n", addr, pages, prot);
    return (int64_t)addr;
}
