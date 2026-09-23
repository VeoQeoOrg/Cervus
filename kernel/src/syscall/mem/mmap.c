#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/io/serial.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

extern int memfd_is(const vnode_t *n);
extern int memfd_page_phys(vnode_t *n, size_t index, uintptr_t *out);

int64_t sys_mmap(uint64_t hint, uint64_t length, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace) return -EINVAL;

    vnode_t *backing = NULL;
    vnode_t *devmap = NULL;
    vfs_file_t *filebacked = NULL;
    if (!(flags & MAP_ANONYMOUS)) {
        if (!t->fd_table) return -EBADF;
        vfs_file_t *file = fd_get(t->fd_table, (int)fd);
        if (!file) return -EBADF;
        if (!file->vnode) { fd_put(file); return -EBADF; }
        if (memfd_is(file->vnode)) {
            backing = file->vnode;
            if (task_map_track(t, backing) < 0) { fd_put(file); return -ENOMEM; }
            fd_put(file);
        } else if (file->vnode->ops && file->vnode->ops->mmap_page) {
            devmap = file->vnode;
            if (task_map_track(t, devmap) < 0) { fd_put(file); return -ENOMEM; }
            fd_put(file);
        } else {
            if ((flags & MAP_SHARED) && (prot & PROT_WRITE)) { fd_put(file); return -ENODEV; }
            filebacked = file;
        }
    } else if (fd != (uint64_t)-1 && fd != 0) {
        return -EINVAL;
    }
    if (!length) return -EINVAL;
    if (length > (1ULL << 40)) return -ENOMEM;

    if (length + 0xFFFULL < length) return -EINVAL;
    size_t pages = (length + 0xFFFULL) >> 12;
    if (pages == 0 || pages > (1ULL << 28)) return -ENOMEM;

    uintptr_t addr;
    if (flags & MAP_FIXED)       addr = hint & ~0xFFFULL;
    else if (hint)               addr = hint & ~0xFFFULL;
    else {
        uint64_t span = (uint64_t)pages * 0x1000;
        if (t->brk_max < span) return -ENOMEM;
        addr = (t->brk_max - span) & ~0xFFFULL;
        if (addr <= t->brk_current) return -ENOMEM;
        t->brk_max = addr;
    }
    if (addr < 0x1000ULL) return -EINVAL;
    if (addr >= 0x0000800000000000ULL) return -ENOMEM;
    uint64_t end_check = addr + (uint64_t)pages * 0x1000;
    if (end_check < addr) return -EINVAL;
    if (end_check > 0x0000800000000000ULL) return -ENOMEM;

    uint64_t vf = VMM_PRESENT | VMM_USER;
    if (prot & PROT_WRITE) vf |= VMM_WRITE;
    if (!(prot & PROT_EXEC)) vf |= VMM_NOEXEC;
    if (backing || devmap) vf |= VMM_SHARED;

    for (size_t i = 0; i < pages; i++) {
        uintptr_t phys;
        if (backing) {
            size_t index = (size_t)((offset >> 12) + i);
            if (memfd_page_phys(backing, index, &phys) < 0) {
                for (size_t j = 0; j < i; j++) vmm_unmap_page(t->pagemap, addr + j * 0x1000);
                return -ENOMEM;
            }
        } else if (devmap) {
            if (devmap->ops->mmap_page(devmap, offset + (uint64_t)i * 0x1000, &phys) < 0) {
                for (size_t j = 0; j < i; j++) vmm_unmap_page(t->pagemap, addr + j * 0x1000);
                return -ENXIO;
            }
        } else if (filebacked) {
            void *ph = pmm_alloc_zero(1);
            if (!ph) {
                for (size_t j = 0; j < i; j++) vmm_unmap_page(t->pagemap, addr + j * 0x1000);
                fd_put(filebacked);
                return -ENOMEM;
            }
            phys = pmm_virt_to_phys(ph);
            int64_t save = vfs_seek(filebacked, 0, 1);
            vfs_seek(filebacked, (int64_t)(offset + (uint64_t)i * 0x1000), 0);
            int64_t got = 0;
            while (got < 0x1000) {
                int64_t r = vfs_read(filebacked, (uint8_t *)ph + got, 0x1000 - (size_t)got);
                if (r <= 0) break;
                got += r;
            }
            vfs_seek(filebacked, save, 0);
            if (got < 0x1000) memset((uint8_t *)ph + got, 0, 0x1000 - (size_t)got);
        } else {
            void *ph = pmm_alloc_zero(1);
            if (!ph) {
                for (size_t j = 0; j < i; j++) vmm_unmap_page(t->pagemap, addr + j * 0x1000);
                return -ENOMEM;
            }
            phys = pmm_virt_to_phys(ph);
        }
        if (!vmm_map_page(t->pagemap, addr + i * 0x1000, phys, vf)) {
            for (size_t j = 0; j < i; j++) vmm_unmap_page(t->pagemap, addr + j * 0x1000);
            if (filebacked) fd_put(filebacked);
            return -ENOMEM;
        }
    }
    if (filebacked) fd_put(filebacked);
    LOG_D("[SYSCALL] mmap: addr=0x%llx pages=%zu prot=0x%llx\n", addr, pages, prot);
    return (int64_t)addr;
}
