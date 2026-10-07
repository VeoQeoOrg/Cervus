#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/mm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/time/clocksource.h"
#include <string.h>

#define FUTEX_WAITERS_MAX 128

typedef struct {
    uint64_t space;
    uint64_t addr;
} futex_key_t;

typedef struct {
    int       used;
    futex_key_t key;
    task_t   *task;
    int       woken;
} futex_waiter_t;

static futex_waiter_t g_waiters[FUTEX_WAITERS_MAX];
static spinlock_t     g_futex_lock = SPINLOCK_INIT;

extern bool signal_pending_deliverable(task_t *t);
extern uint64_t sched_now_ns(void);

static int futex_key(uint64_t uaddr, futex_key_t *out)
{
    task_t *me = syscall_cur_task();
    if (!me || !me->pagemap) return -EFAULT;

    if (me->mm) {
        kmutex_lock(&me->mm->lock);
        vm_area_t *v = mm_find_vma(me->mm, (uintptr_t)uaddr);
        bool shared = v && (v->flags & (VM_SHARED | VM_PFNMAP));
        bool ok = v != NULL;
        kmutex_unlock(&me->mm->lock);
        if (!ok) return -EFAULT;
        if (!shared) {
            out->space = (uint64_t)(uintptr_t)me->mm;
            out->addr  = uaddr;
            return 0;
        }
        uintptr_t phys = 0;
        if (mm_user_phys(me->mm, (uintptr_t)uaddr, 0, &phys) < 0) return -EFAULT;
        out->space = 0;
        out->addr  = phys;
        return 0;
    }

    uintptr_t phys = 0;
    if (!vmm_virt_to_phys(me->pagemap, (uintptr_t)(uaddr & ~0xFFFULL), &phys))
        return -EFAULT;
    out->space = 0;
    out->addr  = (uint64_t)phys | (uaddr & 0xFFFULL);
    return 0;
}

static volatile uint32_t *futex_word(task_t *me, uint64_t uaddr, bool write, uintptr_t *phys_out);

static bool futex_peek(task_t *me, uint64_t uaddr, uint32_t *out)
{
    volatile uint32_t *p = futex_word(me, uaddr, false, NULL);
    if (!p) return false;
    *out = __atomic_load_n(p, __ATOMIC_ACQUIRE);
    return true;
}

int64_t sys_futex_wait(uint64_t uaddr, uint64_t expected, uint64_t timeout_ns)
{
    if (uaddr & 3) return -EINVAL;
    if (!syscall_uptr_validate((void *)uaddr, sizeof(uint32_t))) return -EFAULT;

    futex_key_t key;
    int kr = futex_key(uaddr, &key);
    if (kr < 0) return kr;

    task_t *me = syscall_cur_task();
    if (!me) return -EFAULT;

    uint64_t f;
    uint32_t seen = 0;
    for (int tries = 0; ; tries++) {
        f = spinlock_acquire_irqsave(&g_futex_lock);
        if (futex_peek(me, uaddr, &seen)) break;
        spinlock_release_irqrestore(&g_futex_lock, f);
        if (tries >= 3 || !syscall_uptr_validate((void *)uaddr, sizeof(uint32_t))) return -EFAULT;
    }
    if (seen != (uint32_t)expected) {
        spinlock_release_irqrestore(&g_futex_lock, f);
        return -EAGAIN;
    }

    int slot = -1;
    for (int i = 0; i < FUTEX_WAITERS_MAX; i++)
        if (!g_waiters[i].used) { slot = i; break; }
    if (slot < 0) {
        spinlock_release_irqrestore(&g_futex_lock, f);
        return -ENOMEM;
    }

    g_waiters[slot].used  = 1;
    g_waiters[slot].key   = key;
    g_waiters[slot].task  = me;
    g_waiters[slot].woken = 0;

    me->wakeup_time_ns = timeout_ns ? (sched_now_ns() + timeout_ns) : 0;
    me->runnable = false;
    me->state    = TASK_BLOCKED;

    spinlock_release_irqrestore(&g_futex_lock, f);

    sched_reschedule();

    f = spinlock_acquire_irqsave(&g_futex_lock);
    int woken = g_waiters[slot].woken;
    g_waiters[slot].used = 0;
    g_waiters[slot].task = NULL;
    spinlock_release_irqrestore(&g_futex_lock, f);

    me->wakeup_time_ns = 0;

    if (woken) return 0;
    if (me->pending_kill) return -EINTR;
    if (signal_pending_deliverable(me)) return -EINTR;
    if (timeout_ns) return -ETIMEDOUT;
    return 0;
}

static int64_t futex_wake_key(const futex_key_t *key, uint64_t count)
{
    int64_t woken = 0;
    uint64_t f = spinlock_acquire_irqsave(&g_futex_lock);
    for (int i = 0; i < FUTEX_WAITERS_MAX && (uint64_t)woken < count; i++) {
        if (!g_waiters[i].used || g_waiters[i].key.space != key->space ||
            g_waiters[i].key.addr != key->addr) continue;
        if (g_waiters[i].woken) continue;
        task_t *t = g_waiters[i].task;
        g_waiters[i].woken = 1;
        if (t) {
            t->wakeup_time_ns = 0;
            task_unblock(t);
        }
        woken++;
    }
    spinlock_release_irqrestore(&g_futex_lock, f);
    return woken;
}

static volatile uint32_t *futex_word(task_t *me, uint64_t uaddr, bool write, uintptr_t *phys_out)
{
    uintptr_t phys = 0;
    uint64_t flags = 0;
    if (!vmm_virt_to_phys(me->pagemap, (uintptr_t)(uaddr & ~0xFFFULL), &phys)) return NULL;
    if (write && (!vmm_virt_flags(me->pagemap, (uintptr_t)(uaddr & ~0xFFFULL), &flags) || !(flags & VMM_WRITE)))
        return NULL;
    phys = (phys & ~0xFFFULL) + (uaddr & 0xFFFULL);
    if (phys_out) *phys_out = phys;
    return pmm_phys_to_virt(phys);
}

void thread_clear_tid(task_t *me)
{
    uint64_t uaddr = me ? me->clear_tid : 0;
    if (!uaddr || !me->pagemap) return;
    me->clear_tid = 0;

    uintptr_t phys = 0;
    volatile uint32_t *w = futex_word(me, uaddr, true, &phys);
    if (!w && syscall_uptr_validate_write((const void *)(uintptr_t)uaddr, sizeof(uint32_t)))
        w = futex_word(me, uaddr, true, &phys);
    if (!w) return;
    __atomic_store_n(w, 0, __ATOMIC_RELEASE);

    futex_key_t key;
    if (me->mm) {
        key.space = (uint64_t)(uintptr_t)me->mm;
        key.addr  = uaddr;
    } else {
        key.space = 0;
        key.addr  = phys;
    }
    futex_wake_key(&key, UINT64_MAX);
}

int64_t sys_futex_wake(uint64_t uaddr, uint64_t count, uint64_t unused)
{
    (void)unused;
    if (uaddr & 3) return -EINVAL;
    if (!syscall_uptr_validate((void *)uaddr, sizeof(uint32_t))) return -EFAULT;

    futex_key_t key;
    int kr = futex_key(uaddr, &key);
    if (kr < 0) return kr;

    if (count == 0) count = 1;
    return futex_wake_key(&key, count);
}

void futex_task_exit(task_t *who)
{
    if (!who) return;
    uint64_t f = spinlock_acquire_irqsave(&g_futex_lock);
    for (int i = 0; i < FUTEX_WAITERS_MAX; i++)
        if (g_waiters[i].used && g_waiters[i].task == who) {
            g_waiters[i].used = 0;
            g_waiters[i].task = NULL;
        }
    spinlock_release_irqrestore(&g_futex_lock, f);
}
