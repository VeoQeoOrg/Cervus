#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/memory/mm.h"
#include "../../../include/sched/spinlock.h"
#include <string.h>

#define SHM_MAX      32
#define IPC_CREAT    01000
#define IPC_RMID     0
#define SHM_RDONLY   010000

typedef struct { int used, key, id; void *mem; size_t pages, size; int nattach, marked; } shm_seg_t;

static shm_seg_t g_shm[SHM_MAX];
static spinlock_t g_lock = SPINLOCK_INIT;
static int g_next_id = 1;

static shm_seg_t *seg_by_id(int id) {
    for (int i = 0; i < SHM_MAX; i++) if (g_shm[i].used && g_shm[i].id == id) return &g_shm[i];
    return 0;
}

int64_t sys_shmget(uint64_t key, uint64_t size, uint64_t flags) {
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    if (key != 0) {
        for (int i = 0; i < SHM_MAX; i++) if (g_shm[i].used && g_shm[i].key == (int)key) { int id = g_shm[i].id; spinlock_release_irqrestore(&g_lock, f); return id; }
        if (!(flags & IPC_CREAT)) { spinlock_release_irqrestore(&g_lock, f); return -ENOENT; }
    }
    if (size == 0 || size > (16ULL << 20)) { spinlock_release_irqrestore(&g_lock, f); return -EINVAL; }
    size_t pages = (size + 0xFFF) >> 12;
    int slot = -1; for (int i = 0; i < SHM_MAX; i++) if (!g_shm[i].used) { slot = i; break; }
    if (slot < 0) { spinlock_release_irqrestore(&g_lock, f); return -ENOSPC; }
    void *mem = pmm_alloc_zero(pages);
    if (!mem) { spinlock_release_irqrestore(&g_lock, f); return -ENOMEM; }
    g_shm[slot].used = 1; g_shm[slot].key = (int)key; g_shm[slot].id = g_next_id++;
    g_shm[slot].mem = mem; g_shm[slot].pages = pages; g_shm[slot].size = size;
    g_shm[slot].nattach = 0; g_shm[slot].marked = 0;
    int id = g_shm[slot].id;
    spinlock_release_irqrestore(&g_lock, f);
    return id;
}

static int shm_pfn(vm_area_t *v, uint64_t off, uintptr_t *phys) {
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    shm_seg_t *sg = seg_by_id((int)v->priv_id);
    uintptr_t base = sg ? pmm_virt_to_phys(sg->mem) : 0;
    size_t pages = sg ? sg->pages : 0;
    spinlock_release_irqrestore(&g_lock, f);
    if (!base || (off >> 12) >= pages) return -EFAULT;
    *phys = base + off;
    return 0;
}

static void shm_vma_open(vm_area_t *v) {
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    shm_seg_t *sg = seg_by_id((int)v->priv_id);
    if (sg) sg->nattach++;
    spinlock_release_irqrestore(&g_lock, f);
}

static void shm_vma_close(vm_area_t *v) {
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    shm_seg_t *sg = seg_by_id((int)v->priv_id);
    if (sg) {
        sg->nattach--;
        if (sg->marked && sg->nattach <= 0) { pmm_free(sg->mem, sg->pages); sg->used = 0; }
    }
    spinlock_release_irqrestore(&g_lock, f);
}

int64_t sys_shmat(uint64_t id, uint64_t addr, uint64_t flags) {
    task_t *t = syscall_cur_task();
    if (!t || !t->mm || !t->is_userspace) return -EACCES;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    shm_seg_t *sg = seg_by_id((int)id);
    size_t pages = sg ? sg->pages : 0;
    spinlock_release_irqrestore(&g_lock, f);
    if (!sg) return -EINVAL;
    uint32_t vmf = VM_READ | VM_SHARED;
    if (!(flags & SHM_RDONLY)) vmf |= VM_WRITE;
    return mm_map_pfn(t->mm, addr & ~0xFFFULL, pages * 0x1000, vmf, addr != 0, 0,
                      shm_pfn, shm_vma_open, shm_vma_close, NULL, id, NULL, 0);
}

int64_t sys_shmdt(uint64_t addr) {
    task_t *t = syscall_cur_task();
    if (!t || !t->mm) return -EINVAL;
    uintptr_t va = addr & ~0xFFFULL;
    kmutex_lock(&t->mm->lock);
    vm_area_t *v = mm_find_vma(t->mm, va);
    bool ok = v && v->close == shm_vma_close && v->start == va;
    uintptr_t len = ok ? v->end - v->start : 0;
    kmutex_unlock(&t->mm->lock);
    if (!ok) return -EINVAL;
    return mm_unmap(t->mm, va, len);
}

int64_t sys_shmctl(uint64_t id, uint64_t cmd, uint64_t buf) {
    (void)buf;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    shm_seg_t *s = seg_by_id((int)id);
    if (!s) { spinlock_release_irqrestore(&g_lock, f); return -EINVAL; }
    if (cmd == IPC_RMID) {
        s->marked = 1;
        if (s->nattach <= 0) { pmm_free(s->mem, s->pages); s->used = 0; }
    }
    spinlock_release_irqrestore(&g_lock, f);
    return 0;
}

void shm_task_exit(task_t *t) {
    (void)t;
}
