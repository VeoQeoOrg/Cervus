#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/memory/mm.h"
#include "../../../include/io/serial.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

#define MAP_TYPE_MASK       0x0F
#define MAP_POPULATE_FLAG   0x8000
#define MAP_FIXED_NOREPLACE 0x100000

extern int memfd_is(const vnode_t *n);
extern int memfd_page_phys(vnode_t *n, size_t index, uintptr_t *out);
extern vnode_t *memfd_create_anon(const char *name);

static int memfd_pfn(vm_area_t *v, uint64_t off, uintptr_t *phys) {
    return memfd_page_phys(v->vnode, (size_t)(off >> 12), phys) < 0 ? -ENOMEM : 0;
}

static int device_pfn(vm_area_t *v, uint64_t off, uintptr_t *phys) {
    if (!v->vnode || !v->vnode->ops || !v->vnode->ops->mmap_page) return -ENXIO;
    return v->vnode->ops->mmap_page(v->vnode, off, phys) < 0 ? -ENXIO : 0;
}

static uint32_t prot_to_vm(uint64_t prot) {
    uint32_t f = 0;
    if (prot & PROT_READ)  f |= VM_READ;
    if (prot & PROT_WRITE) f |= VM_WRITE | VM_READ;
    if (prot & PROT_EXEC)  f |= VM_EXEC | VM_READ;
    return f;
}

int64_t sys_mmap(uint64_t hint, uint64_t length, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->is_userspace || !t->mm) return -EINVAL;
    if (!length) return -EINVAL;
    if (length > (1ULL << 46)) return -ENOMEM;
    if (offset & 0xFFFULL) return -EINVAL;
    uint64_t type = flags & MAP_TYPE_MASK;
    if (type != MAP_SHARED && type != MAP_PRIVATE) return -EINVAL;

    bool fixed = (flags & MAP_FIXED) != 0;
    if (flags & MAP_FIXED_NOREPLACE) {
        uintptr_t a = hint & ~0xFFFULL;
        uintptr_t e = a + ((length + 0xFFF) & ~0xFFFULL);
        for (uintptr_t p = a; p < e; p += 0x1000)
            if (mm_find_vma(t->mm, p)) return -EEXIST;
        fixed = true;
    }
    if (fixed && (hint & 0xFFFULL)) return -EINVAL;
    uint64_t as = t->rlim[RLIMIT_AS_K].cur;
    if (as != RLIM_INF_K && mm_vm_bytes(t->mm) + ((length + 0xFFF) & ~0xFFFULL) > as) return -ENOMEM;

    uint32_t vmf = prot_to_vm(prot);
    if (type == MAP_SHARED) vmf |= VM_SHARED;

    int64_t r;
    if (flags & MAP_ANONYMOUS) {
        if (type == MAP_SHARED) {
            vnode_t *vn = memfd_create_anon("anon-shared");
            if (!vn) return -ENOMEM;
            r = mm_map_pfn(t->mm, hint, length, vmf, fixed, 0, memfd_pfn, NULL, NULL,
                           NULL, 0, vn, 0);
            vnode_unref(vn);
        } else {
            r = mm_map_anon(t->mm, hint, length, vmf, fixed);
        }
    } else {
        if (!t->fd_table) return -EBADF;
        vfs_file_t *file = fd_get(t->fd_table, (int)fd);
        if (!file) return -EBADF;
        vnode_t *vn = file->vnode;
        if (!vn) { fd_put(file); return -EBADF; }
        int acc = file->flags & 3;
        if ((prot & PROT_READ) && acc == 1) { fd_put(file); return -EACCES; }
        if (memfd_is(vn) && type == MAP_SHARED) {
            r = mm_map_pfn(t->mm, hint, length, vmf, fixed, 0, memfd_pfn, NULL, NULL,
                           NULL, 0, vn, offset);
        } else if (vn->ops && vn->ops->mmap_page) {
            r = mm_map_pfn(t->mm, hint, length, vmf | VM_SHARED, fixed, 0, device_pfn, NULL, NULL,
                           NULL, 0, vn, offset);
        } else if (vn->type == VFS_NODE_FILE && vn->ops && vn->ops->read) {
            if (type == MAP_SHARED && (prot & PROT_WRITE) && acc == 0) { fd_put(file); return -EACCES; }
            r = mm_map_file(t->mm, hint, length, vmf, fixed, vn, offset);
        } else {
            r = -ENODEV;
        }
        fd_put(file);
    }
    if (r < 0) return r;
    if (flags & MAP_POPULATE_FLAG) mm_populate(t->mm, (uintptr_t)r, length, (prot & PROT_WRITE) && type == MAP_PRIVATE);
    return r;
}

int64_t sys_mprotect(uint64_t addr, uint64_t len, uint64_t prot)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    if (addr & 0xFFF) return -EINVAL;
    return mm_protect(t->mm, addr, len, prot_to_vm(prot));
}

int64_t sys_madvise(uint64_t addr, uint64_t len, uint64_t advice)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    if (addr & 0xFFF) return -EINVAL;
    switch (advice) {
        case 4:
        case 8:
            return mm_discard(t->mm, addr, len);
        default:
            return 0;
    }
}

int64_t sys_msync(uint64_t addr, uint64_t len, uint64_t flags)
{
    (void)flags;
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    if (addr & 0xFFF) return -EINVAL;
    return mm_sync(t->mm, addr, len);
}

int64_t sys_mlock(uint64_t addr, uint64_t len)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    if (t->uid != 0) {
        uint64_t lim = t->rlim[RLIMIT_MEMLOCK_K].cur;
        if (lim == 0) return -EPERM;
        if (lim != RLIM_INF_K && mm_locked_bytes(t->mm) + len > lim) return -ENOMEM;
    }
    return mm_mlock(t->mm, addr, len, true);
}

int64_t sys_munlock(uint64_t addr, uint64_t len)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    return mm_mlock(t->mm, addr, len, false);
}

int64_t sys_mlockall(uint64_t flags)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    if (!flags || (flags & ~7ULL)) return -EINVAL;
    if (t->uid != 0) {
        uint64_t lim = t->rlim[RLIMIT_MEMLOCK_K].cur;
        if (lim == 0) return -EPERM;
        if (lim != RLIM_INF_K && (flags & 1) && mm_vm_bytes(t->mm) > lim) return -ENOMEM;
    }
    return mm_mlockall(t->mm, (int)flags, true);
}

int64_t sys_munlockall(void)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    return mm_mlockall(t->mm, 0, false);
}
