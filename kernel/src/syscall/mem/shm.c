#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/sched/spinlock.h"
#include <string.h>

#define SHM_MAX      32
#define SHM_ATT_MAX  128
#define IPC_CREAT    01000
#define IPC_RMID     0
#define SHM_RDONLY   010000

typedef struct { int used, key, id; void *mem; size_t pages, size; int nattach, marked; } shm_seg_t;
typedef struct { int used; task_t *task; uintptr_t vaddr; int id; } shm_att_t;

static shm_seg_t g_shm[SHM_MAX];
static shm_att_t g_att[SHM_ATT_MAX];
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

int64_t sys_shmat(uint64_t id, uint64_t addr, uint64_t flags) {
    (void)addr;
    task_t *t = syscall_cur_task();
    if (!t || !t->pagemap || !t->is_userspace) return -EACCES;

    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    shm_seg_t *s = seg_by_id((int)id);
    if (!s) { spinlock_release_irqrestore(&g_lock, f); return -EINVAL; }
    size_t pages = s->pages;
    void *mem = s->mem;
    int aslot = -1; for (int i = 0; i < SHM_ATT_MAX; i++) if (!g_att[i].used) { aslot = i; break; }
    if (aslot < 0) { spinlock_release_irqrestore(&g_lock, f); return -ENOSPC; }
    uint64_t span = (uint64_t)pages * 0x1000;
    if (t->brk_max < span) { spinlock_release_irqrestore(&g_lock, f); return -ENOMEM; }
    uintptr_t uaddr = (t->brk_max - span) & ~0xFFFULL;
    if (uaddr <= t->brk_current) { spinlock_release_irqrestore(&g_lock, f); return -ENOMEM; }
    t->brk_max = uaddr;
    g_att[aslot].used = 1; g_att[aslot].task = t; g_att[aslot].vaddr = uaddr; g_att[aslot].id = (int)id;
    s->nattach++;
    spinlock_release_irqrestore(&g_lock, f);

    uint64_t vf = VMM_PRESENT | VMM_USER | VMM_NOEXEC | VMM_SHARED;
    if (!(flags & SHM_RDONLY)) vf |= VMM_WRITE;
    for (size_t i = 0; i < pages; i++) {
        if (!vmm_map_page(t->pagemap, uaddr + i * 0x1000, pmm_virt_to_phys((char *)mem + i * 0x1000), vf)) {
            for (size_t j = 0; j < i; j++) vmm_unmap_page(t->pagemap, uaddr + j * 0x1000);
            uint64_t g = spinlock_acquire_irqsave(&g_lock);
            g_att[aslot].used = 0;
            shm_seg_t *s2 = seg_by_id((int)id); if (s2) s2->nattach--;
            spinlock_release_irqrestore(&g_lock, g);
            return -ENOMEM;
        }
    }
    return (int64_t)uaddr;
}

int64_t sys_shmdt(uint64_t addr) {
    task_t *t = syscall_cur_task();
    if (!t || !t->pagemap) return -EINVAL;
    uintptr_t va = addr & ~0xFFFULL;

    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    int aslot = -1;
    for (int i = 0; i < SHM_ATT_MAX; i++) if (g_att[i].used && g_att[i].task == t && g_att[i].vaddr == va) { aslot = i; break; }
    if (aslot < 0) { spinlock_release_irqrestore(&g_lock, f); return -EINVAL; }
    int id = g_att[aslot].id;
    g_att[aslot].used = 0;
    shm_seg_t *s = seg_by_id(id);
    size_t pages = s ? s->pages : 0;
    spinlock_release_irqrestore(&g_lock, f);

    for (size_t i = 0; i < pages; i++) vmm_unmap_page(t->pagemap, va + i * 0x1000);

    uint64_t g = spinlock_acquire_irqsave(&g_lock);
    s = seg_by_id(id);
    if (s) { s->nattach--; if (s->marked && s->nattach <= 0) { pmm_free(s->mem, s->pages); s->used = 0; } }
    spinlock_release_irqrestore(&g_lock, g);
    return 0;
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
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    for (int i = 0; i < SHM_ATT_MAX; i++) {
        if (g_att[i].used && g_att[i].task == t) {
            g_att[i].used = 0;
            shm_seg_t *s = seg_by_id(g_att[i].id);
            if (s) { s->nattach--; if (s->marked && s->nattach <= 0) { pmm_free(s->mem, s->pages); s->used = 0; } }
        }
    }
    spinlock_release_irqrestore(&g_lock, f);
}
