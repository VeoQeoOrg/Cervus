#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/time/clocksource.h"
#include <string.h>

#define FUTEX_WAITERS_MAX 128

typedef struct {
    int       used;
    uint64_t  key;
    task_t   *task;
    int       woken;
} futex_waiter_t;

static futex_waiter_t g_waiters[FUTEX_WAITERS_MAX];
static spinlock_t     g_futex_lock = SPINLOCK_INIT;

extern bool signal_pending_deliverable(task_t *t);
extern uint64_t sched_now_ns(void);

static int futex_key(uint64_t uaddr, uint64_t *out)
{
    task_t *me = syscall_cur_task();
    if (!me || !me->pagemap) return -EFAULT;

    uintptr_t phys = 0;
    if (!vmm_virt_to_phys(me->pagemap, (uintptr_t)(uaddr & ~0xFFFULL), &phys))
        return -EFAULT;

    *out = (uint64_t)phys | (uaddr & 0xFFFULL);
    return 0;
}

int64_t sys_futex_wait(uint64_t uaddr, uint64_t expected, uint64_t timeout_ns)
{
    if (uaddr & 3) return -EINVAL;
    if (!syscall_uptr_validate((void *)uaddr, sizeof(uint32_t))) return -EFAULT;

    uint64_t key;
    int kr = futex_key(uaddr, &key);
    if (kr < 0) return kr;

    task_t *me = syscall_cur_task();
    if (!me) return -EFAULT;

    uint64_t f = spinlock_acquire_irqsave(&g_futex_lock);

    uint32_t seen = 0;
    if (syscall_copy_from_user(&seen, (void *)uaddr, sizeof seen) < 0) {
        spinlock_release_irqrestore(&g_futex_lock, f);
        return -EFAULT;
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

int64_t sys_futex_wake(uint64_t uaddr, uint64_t count, uint64_t unused)
{
    (void)unused;
    if (uaddr & 3) return -EINVAL;
    if (!syscall_uptr_validate((void *)uaddr, sizeof(uint32_t))) return -EFAULT;

    uint64_t key;
    int kr = futex_key(uaddr, &key);
    if (kr < 0) return kr;

    if (count == 0) count = 1;

    int64_t woken = 0;
    uint64_t f = spinlock_acquire_irqsave(&g_futex_lock);
    for (int i = 0; i < FUTEX_WAITERS_MAX && (uint64_t)woken < count; i++) {
        if (!g_waiters[i].used || g_waiters[i].key != key) continue;
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
