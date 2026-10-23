#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/mm.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/time/clocksource.h"
#include <string.h>

#define FUTEX_WAITERS_MAX 1024

typedef struct {
    uint64_t space;
    uint64_t addr;
} futex_key_t;

typedef struct {
    int       used;
    futex_key_t key;
    task_t   *task;
    int       woken;
    uint32_t  bitset;
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

static int64_t futex_wait_until(uint64_t uaddr, uint32_t expected, uint64_t deadline, uint32_t bitset)
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
    g_waiters[slot].bitset = bitset;

    me->wakeup_time_ns = deadline;
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
    if (deadline) return -ETIMEDOUT;
    return 0;
}

int64_t sys_futex_wait(uint64_t uaddr, uint64_t expected, uint64_t timeout_ns)
{
    return futex_wait_until(uaddr, (uint32_t)expected,
                            timeout_ns ? sched_now_ns() + timeout_ns : 0, UINT32_MAX);
}

static int64_t futex_wake_locked(const futex_key_t *key, uint64_t count, uint32_t bitset)
{
    int64_t woken = 0;
    for (int i = 0; i < FUTEX_WAITERS_MAX && (uint64_t)woken < count; i++) {
        if (!g_waiters[i].used || g_waiters[i].key.space != key->space ||
            g_waiters[i].key.addr != key->addr) continue;
        if (g_waiters[i].woken || !(g_waiters[i].bitset & bitset)) continue;
        task_t *t = g_waiters[i].task;
        g_waiters[i].woken = 1;
        if (t) {
            t->wakeup_time_ns = 0;
            task_unblock(t);
        }
        woken++;
    }
    return woken;
}

static int64_t futex_wake_key(const futex_key_t *key, uint64_t count)
{
    uint64_t f = spinlock_acquire_irqsave(&g_futex_lock);
    int64_t woken = futex_wake_locked(key, count, UINT32_MAX);
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

void thread_set_tid(task_t *parent, uint64_t uaddr, uint32_t tid)
{
    volatile uint32_t *w = futex_word(parent, uaddr, true, NULL);
    if (!w && syscall_uptr_validate_write((const void *)(uintptr_t)uaddr, sizeof(uint32_t)))
        w = futex_word(parent, uaddr, true, NULL);
    if (w) __atomic_store_n(w, tid, __ATOMIC_RELEASE);
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

extern int64_t clock_realtime_offset_ns(void);

typedef struct { int64_t tv_sec; int64_t tv_nsec; } futex_ts_t;

static int futex_read_ts(uint64_t uts, futex_ts_t *ts)
{
    if (syscall_copy_from_user(ts, (const void *)uts, sizeof(*ts)) < 0) return -EFAULT;
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL) return -EINVAL;
    return 0;
}

static uint64_t futex_ts_ns(const futex_ts_t *ts)
{
    if ((uint64_t)ts->tv_sec > UINT64_MAX / 1000000000ULL - 1) return UINT64_MAX;
    return (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
}

static int futex_op_apply(uint32_t encoded, volatile uint32_t *w, uint32_t *old_out)
{
    uint32_t op  = (encoded >> 28) & 7;
    uint32_t arg = (encoded >> 12) & 0xFFF;
    if (encoded & (8U << 28)) {
        if (arg > 31) return -EINVAL;
        arg = 1U << arg;
    }
    uint32_t old = __atomic_load_n(w, __ATOMIC_RELAXED), nv;
    do {
        switch (op) {
        case 0: nv = arg; break;
        case 1: nv = old + arg; break;
        case 2: nv = old | arg; break;
        case 3: nv = old & ~arg; break;
        case 4: nv = old ^ arg; break;
        default: return -ENOSYS;
        }
    } while (!__atomic_compare_exchange_n(w, &old, nv, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
    *old_out = old;
    return 0;
}

static bool futex_op_cmp(uint32_t encoded, uint32_t old)
{
    int32_t cmparg = (int32_t)(encoded << 20) >> 20;
    int32_t v = (int32_t)old;
    switch ((encoded >> 24) & 15) {
    case 0: return v == cmparg;
    case 1: return v != cmparg;
    case 2: return v <  cmparg;
    case 3: return v <= cmparg;
    case 4: return v >  cmparg;
    case 5: return v >= cmparg;
    default: return false;
    }
}

static int64_t futex_requeue(uint64_t uaddr, uint64_t uaddr2, uint32_t nwake, uint32_t nreq,
                             bool cmp, uint32_t cmpval)
{
    if ((uaddr | uaddr2) & 3) return -EINVAL;
    if (!syscall_uptr_validate((void *)uaddr, 4) || !syscall_uptr_validate((void *)uaddr2, 4))
        return -EFAULT;
    futex_key_t k1, k2;
    int kr = futex_key(uaddr, &k1);
    if (kr < 0) return kr;
    if ((kr = futex_key(uaddr2, &k2)) < 0) return kr;

    task_t *me = syscall_cur_task();
    uint64_t f = spinlock_acquire_irqsave(&g_futex_lock);
    if (cmp) {
        uint32_t seen;
        if (!futex_peek(me, uaddr, &seen)) {
            spinlock_release_irqrestore(&g_futex_lock, f);
            return -EFAULT;
        }
        if (seen != cmpval) {
            spinlock_release_irqrestore(&g_futex_lock, f);
            return -EAGAIN;
        }
    }
    int64_t done = futex_wake_locked(&k1, nwake, UINT32_MAX);
    uint32_t moved = 0;
    for (int i = 0; i < FUTEX_WAITERS_MAX && moved < nreq; i++) {
        futex_waiter_t *w = &g_waiters[i];
        if (!w->used || w->woken || w->key.space != k1.space || w->key.addr != k1.addr) continue;
        w->key = k2;
        moved++;
    }
    spinlock_release_irqrestore(&g_futex_lock, f);
    return done + (cmp ? (int64_t)moved : 0);
}

static int64_t futex_wake_op(uint64_t uaddr, uint64_t uaddr2, uint32_t n1, uint32_t n2, uint32_t encoded)
{
    if ((uaddr | uaddr2) & 3) return -EINVAL;
    if (!syscall_uptr_validate((void *)uaddr, 4) ||
        !syscall_uptr_validate_write((void *)uaddr2, 4)) return -EFAULT;
    futex_key_t k1, k2;
    int kr = futex_key(uaddr, &k1);
    if (kr < 0) return kr;
    if ((kr = futex_key(uaddr2, &k2)) < 0) return kr;

    task_t *me = syscall_cur_task();
    uint64_t f;
    volatile uint32_t *w2 = NULL;
    for (int tries = 0; ; tries++) {
        f = spinlock_acquire_irqsave(&g_futex_lock);
        if ((w2 = futex_word(me, uaddr2, true, NULL))) break;
        spinlock_release_irqrestore(&g_futex_lock, f);
        if (tries >= 3 || !syscall_uptr_validate_write((void *)uaddr2, 4)) return -EFAULT;
    }
    uint32_t old;
    int r = futex_op_apply(encoded, w2, &old);
    if (r < 0) {
        spinlock_release_irqrestore(&g_futex_lock, f);
        return r;
    }
    int64_t woken = futex_wake_locked(&k1, n1, UINT32_MAX);
    if (futex_op_cmp(encoded, old))
        woken += futex_wake_locked(&k2, n2, UINT32_MAX);
    spinlock_release_irqrestore(&g_futex_lock, f);
    return woken;
}

int64_t sys_futex(uint64_t uaddr, uint64_t op, uint64_t val, uint64_t utime,
                  uint64_t uaddr2, uint64_t val3)
{
    uint32_t cmd = (uint32_t)op & 127;
    bool realtime = (op & 256) != 0;
    if (realtime && cmd != 9 && cmd != 11 && cmd != 0) return -ENOSYS;

    switch (cmd) {
    case 0:
    case 9: {
        uint32_t bitset = cmd == 9 ? (uint32_t)val3 : UINT32_MAX;
        if (!bitset) return -EINVAL;
        uint64_t deadline = 0;
        if (utime) {
            futex_ts_t ts;
            int r = futex_read_ts(utime, &ts);
            if (r < 0) return r;
            uint64_t ns = futex_ts_ns(&ts);
            uint64_t now = sched_now_ns();
            if (cmd == 0) {
                deadline = ns > UINT64_MAX - now ? UINT64_MAX : now + ns;
            } else {
                if (realtime) {
                    int64_t off = clock_realtime_offset_ns();
                    ns = (int64_t)ns > off ? (uint64_t)((int64_t)ns - off) : 0;
                }
                deadline = ns;
            }
            if (!deadline) deadline = 1;
        }
        return futex_wait_until(uaddr, (uint32_t)val, deadline, bitset);
    }
    case 1:
    case 10: {
        uint32_t bitset = cmd == 10 ? (uint32_t)val3 : UINT32_MAX;
        if (!bitset) return -EINVAL;
        if (uaddr & 3) return -EINVAL;
        if (!syscall_uptr_validate((void *)uaddr, 4)) return -EFAULT;
        futex_key_t key;
        int kr = futex_key(uaddr, &key);
        if (kr < 0) return kr;
        uint64_t f = spinlock_acquire_irqsave(&g_futex_lock);
        int64_t n = futex_wake_locked(&key, (uint32_t)val, bitset);
        spinlock_release_irqrestore(&g_futex_lock, f);
        return n;
    }
    case 3:
        return futex_requeue(uaddr, uaddr2, (uint32_t)val, (uint32_t)utime, false, 0);
    case 4:
        return futex_requeue(uaddr, uaddr2, (uint32_t)val, (uint32_t)utime, true, (uint32_t)val3);
    case 5:
        return futex_wake_op(uaddr, uaddr2, (uint32_t)val, (uint32_t)utime, (uint32_t)val3);
    default:
        return -ENOSYS;
    }
}

int64_t futex_wait_deadline(uint64_t uaddr, uint32_t expected, uint64_t deadline)
{
    return futex_wait_until(uaddr, expected, deadline, UINT32_MAX);
}

static volatile uint32_t *futex_user_word(uint64_t uaddr, bool write)
{
    task_t *me = syscall_cur_task();
    if (!me || !me->pagemap || (uaddr & 3)) return NULL;
    volatile uint32_t *w = futex_word(me, uaddr, write, NULL);
    if (w) return w;
    bool ok = write ? syscall_uptr_validate_write((const void *)(uintptr_t)uaddr, 4)
                    : syscall_uptr_validate((const void *)(uintptr_t)uaddr, 4);
    return ok ? futex_word(me, uaddr, write, NULL) : NULL;
}

int futex_user_load(uint64_t uaddr, uint32_t *out)
{
    volatile uint32_t *w = futex_user_word(uaddr, false);
    if (!w) return -EFAULT;
    *out = __atomic_load_n(w, __ATOMIC_ACQUIRE);
    return 0;
}

int futex_user_cas(uint64_t uaddr, uint32_t *expected, uint32_t desired)
{
    volatile uint32_t *w = futex_user_word(uaddr, true);
    if (!w) return -EFAULT;
    return __atomic_compare_exchange_n(w, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 1 : 0;
}
