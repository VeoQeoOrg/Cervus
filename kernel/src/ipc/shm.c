#include "../../include/ipc/ipc.h"
#include "../../include/sched/sched.h"
#include "../../include/memory/mm.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/syscall_internal.h"
#include <string.h>

#define SHMMAX     (1ULL << 30)
#define SHM_RDONLY 010000
#define SHM_RND    020000
#define SHM_REMAP  040000
#define SHM_EXEC   0100000
#define SHM_LOCK   11
#define SHM_UNLOCK 12
#define SHM_DEST   01000
#define SHM_LOCKED 02000
#define PG         4096ULL

typedef struct {
    ipc_obj_t  o;
    size_t     size;
    size_t     npages;
    uintptr_t *pages;
    int64_t    atime, dtime, ctime;
    uint32_t   cpid, lpid;
    uint64_t   nattch;
} shmseg_t;

typedef struct {
    ipc_perm_user_t perm;
    uint64_t segsz;
    int64_t  atime, dtime, ctime;
    int32_t  cpid, lpid;
    uint64_t nattch;
    uint64_t unused4, unused5;
} shmid_ds_user_t;

static void seg_destroy(ipc_obj_t *o)
{
    shmseg_t *s = (shmseg_t *)o;
    for (size_t i = 0; i < s->npages; i++)
        if (s->pages[i]) pmm_free(pmm_phys_to_virt(s->pages[i]), 1);
    kfree(s->pages);
    kfree(s);
}

static ipc_ns_t g_ns = { .lock = SPINLOCK_INIT, .destroy = seg_destroy };

static ipc_obj_t *seg_create(void *arg, int *err)
{
    uint64_t size = (uint64_t)(uintptr_t)arg;
    if (size < 1 || size > SHMMAX) { *err = -EINVAL; return NULL; }
    shmseg_t *s = kzalloc(sizeof *s);
    if (!s) return NULL;
    s->size = size;
    s->npages = (size + PG - 1) / PG;
    s->pages = kzalloc(s->npages * sizeof(uintptr_t));
    if (!s->pages) { kfree(s); return NULL; }
    task_t *me = syscall_cur_task();
    s->cpid = me ? task_tgid(me) : 0;
    s->ctime = ipc_now();
    return &s->o;
}

static int seg_check(ipc_obj_t *o, void *arg)
{
    return (uint64_t)(uintptr_t)arg > ((shmseg_t *)o)->size ? -EINVAL : 0;
}

int64_t sys_shmget(uint64_t key, uint64_t size, uint64_t flags)
{
    return ipc_get(&g_ns, (int32_t)key, (int)flags, seg_create, seg_check, (void *)(uintptr_t)size);
}

static int seg_pfn(vm_area_t *v, uint64_t off, uintptr_t *phys)
{
    shmseg_t *s = v->priv;
    size_t idx = off / PG;
    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    int r = 0;
    if (idx >= s->npages) {
        r = -EFAULT;
    } else {
        if (!s->pages[idx]) {
            void *p = pmm_alloc_zero(1);
            if (p) s->pages[idx] = pmm_virt_to_phys(p);
        }
        if (s->pages[idx]) *phys = s->pages[idx];
        else r = -ENOMEM;
    }
    spinlock_release_irqrestore(&g_ns.lock, f);
    return r;
}

static void seg_vma_open(vm_area_t *v)
{
    shmseg_t *s = v->priv;
    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    s->o.refs++;
    s->nattch++;
    spinlock_release_irqrestore(&g_ns.lock, f);
}

static void seg_vma_close(vm_area_t *v)
{
    shmseg_t *s = v->priv;
    task_t *me = syscall_cur_task();
    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    s->nattch--;
    s->dtime = ipc_now();
    if (me) s->lpid = task_tgid(me);
    ipc_put_locked(&g_ns, &s->o);
    spinlock_release_irqrestore(&g_ns.lock, f);
}

int64_t sys_shmat(uint64_t id, uint64_t addr, uint64_t flags)
{
    task_t *me = syscall_cur_task();
    if (!me || !me->mm || !me->is_userspace) return -EINVAL;
    if (addr & (PG - 1)) {
        if (!(flags & SHM_RND)) return -EINVAL;
        addr &= ~(PG - 1);
    }

    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    shmseg_t *s = (shmseg_t *)ipc_lookup_locked(&g_ns, (int)id);
    if (!s) { spinlock_release_irqrestore(&g_ns.lock, f); return -EINVAL; }
    int want = (flags & SHM_RDONLY) ? IPC_R : (IPC_R | IPC_W);
    if (!ipc_perm_ok(&s->o, me, want)) { spinlock_release_irqrestore(&g_ns.lock, f); return -EACCES; }
    s->o.refs++;
    size_t len = s->npages * PG;
    spinlock_release_irqrestore(&g_ns.lock, f);

    int64_t r;
    if (addr && !(flags & SHM_REMAP) && !mm_range_free(me->mm, addr, addr + len)) {
        r = -EINVAL;
    } else {
        uint32_t vmf = VM_READ | VM_SHARED;
        if (!(flags & SHM_RDONLY)) vmf |= VM_WRITE;
        if (flags & SHM_EXEC) vmf |= VM_EXEC;
        r = mm_map_pfn(me->mm, addr, len, vmf, addr != 0, 0, seg_pfn, seg_vma_open, seg_vma_close,
                       s, (uint64_t)id, NULL, 0);
    }

    f = spinlock_acquire_irqsave(&g_ns.lock);
    if (r >= 0) {
        s->atime = ipc_now();
        s->lpid = task_tgid(me);
    }
    ipc_put_locked(&g_ns, &s->o);
    spinlock_release_irqrestore(&g_ns.lock, f);
    return r;
}

int64_t sys_shmdt(uint64_t addr)
{
    task_t *me = syscall_cur_task();
    if (!me || !me->mm) return -EINVAL;
    if (addr & (PG - 1)) return -EINVAL;
    mm_t *mm = me->mm;
    kmutex_lock(&mm->lock);
    vm_area_t *v = mm_find_vma(mm, addr);
    if (!v || v->close != seg_vma_close || v->start != addr) {
        kmutex_unlock(&mm->lock);
        return -EINVAL;
    }
    shmseg_t *s = v->priv;
    uintptr_t end = addr + s->npages * PG;
    uintptr_t ranges[64][2];
    int n = 0;
    for (; v && v->start < end && n < 64; v = v->next)
        if (v->close == seg_vma_close && v->priv == s) {
            ranges[n][0] = v->start;
            ranges[n][1] = v->end;
            n++;
        }
    kmutex_unlock(&mm->lock);
    for (int i = 0; i < n; i++) mm_unmap(mm, ranges[i][0], ranges[i][1] - ranges[i][0]);
    return 0;
}

int64_t sys_shmctl(uint64_t id, uint64_t cmd, uint64_t buf)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    cmd &= ~0x100ULL;
    shmid_ds_user_t ds;
    if (cmd == IPC_SET_K && syscall_copy_from_user(&ds, (const void *)buf, sizeof ds) < 0) return -EFAULT;

    int64_t r = 0;
    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    shmseg_t *s = (shmseg_t *)ipc_lookup_locked(&g_ns, (int)id);
    if (!s) { spinlock_release_irqrestore(&g_ns.lock, f); return -EINVAL; }
    switch (cmd) {
    case IPC_STAT_K:
        if (!ipc_perm_ok(&s->o, me, IPC_R)) { r = -EACCES; break; }
        memset(&ds, 0, sizeof ds);
        ipc_fill_perm(&s->o, &ds.perm);
        ds.perm.mode = (uint16_t)(s->o.mode & 07777);
        ds.segsz = s->size;
        ds.atime = s->atime; ds.dtime = s->dtime; ds.ctime = s->ctime;
        ds.cpid = (int32_t)s->cpid; ds.lpid = (int32_t)s->lpid;
        ds.nattch = s->nattch;
        break;
    case IPC_SET_K:
        if (!ipc_owner_ok(&s->o, me)) { r = -EPERM; break; }
        ipc_set_perm(&s->o, &ds.perm);
        s->ctime = ipc_now();
        break;
    case IPC_RMID_K:
        if (!ipc_owner_ok(&s->o, me)) { r = -EPERM; break; }
        s->o.mode |= SHM_DEST;
        ipc_remove_locked(&g_ns, &s->o);
        break;
    case SHM_LOCK:
    case SHM_UNLOCK:
        if (me->uid != 0 && !ipc_owner_ok(&s->o, me)) { r = -EPERM; break; }
        if (cmd == SHM_LOCK) s->o.mode |= SHM_LOCKED; else s->o.mode &= ~SHM_LOCKED;
        break;
    default:
        r = -EINVAL;
    }
    spinlock_release_irqrestore(&g_ns.lock, f);
    if (r == 0 && cmd == IPC_STAT_K && syscall_copy_to_user((void *)buf, &ds, sizeof ds) < 0) return -EFAULT;
    return r;
}
