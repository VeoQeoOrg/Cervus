#include "../../include/fs/pollwait.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/spinlock.h"
#include "../../include/syscall/syscall_internal.h"

#define POLLWAIT_SLOTS 512

extern uint64_t sched_now_ns(void);
extern void     task_sleep_ns(uint64_t ns);
extern void     sched_note_wakeup(uint64_t ns);

static task_t           *g_slots[POLLWAIT_SLOTS];
static spinlock_t        g_lock = SPINLOCK_INIT;
static volatile uint64_t g_seq;
static volatile int      g_nwait;

uint64_t poll_seq(void) {
    return __atomic_load_n(&g_seq, __ATOMIC_ACQUIRE);
}

void poll_notify(void) {
    __atomic_add_fetch(&g_seq, 1, __ATOMIC_RELEASE);
    if (!__atomic_load_n(&g_nwait, __ATOMIC_ACQUIRE)) return;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    for (int i = 0; i < POLLWAIT_SLOTS; i++) {
        task_t *t = g_slots[i];
        if (!t) continue;
        g_slots[i] = NULL;
        t->wakeup_time_ns = 0;
        task_unblock(t);
    }
    spinlock_release_irqrestore(&g_lock, f);
}

void poll_wait_since(uint64_t seen, uint64_t deadline_ns) {
    task_t *me = syscall_cur_task();
    if (!me) return;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    if (__atomic_load_n(&g_seq, __ATOMIC_ACQUIRE) != seen) {
        spinlock_release_irqrestore(&g_lock, f);
        return;
    }
    int slot = -1;
    for (int i = 0; i < POLLWAIT_SLOTS; i++)
        if (!g_slots[i]) { slot = i; break; }
    if (slot < 0) {
        spinlock_release_irqrestore(&g_lock, f);
        task_sleep_ns(1000000ULL);
        return;
    }
    g_slots[slot] = me;
    __atomic_add_fetch(&g_nwait, 1, __ATOMIC_ACQ_REL);
    me->wakeup_time_ns = deadline_ns;
    me->runnable = false;
    me->state = TASK_BLOCKED;
    if (deadline_ns) sched_note_wakeup(deadline_ns);
    spinlock_release_irqrestore(&g_lock, f);

    sched_reschedule();

    me->wakeup_time_ns = 0;
    f = spinlock_acquire_irqsave(&g_lock);
    if (g_slots[slot] == me) g_slots[slot] = NULL;
    __atomic_sub_fetch(&g_nwait, 1, __ATOMIC_ACQ_REL);
    spinlock_release_irqrestore(&g_lock, f);
}
