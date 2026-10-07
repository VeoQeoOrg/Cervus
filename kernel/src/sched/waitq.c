#include "../../include/sched/waitq.h"
#include "../../include/sched/sched.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/drivers/timer.h"

static int waitq_add(waitq_t *q, task_t *me)
{
    for (int i = 0; i < WAITQ_SLOTS; i++)
        if (q->slot[i] == me) return 1;
    for (int i = 0; i < WAITQ_SLOTS; i++)
        if (!q->slot[i]) { q->slot[i] = me; return 1; }
    return 0;
}

static void waitq_del(waitq_t *q, task_t *me)
{
    for (int i = 0; i < WAITQ_SLOTS; i++)
        if (q->slot[i] == me) q->slot[i] = NULL;
}

void waitq_wake_all(waitq_t *q)
{
    for (int i = 0; i < WAITQ_SLOTS; i++) {
        task_t *w = q->slot[i];
        if (!w) continue;
        q->slot[i] = NULL;
        task_unblock(w);
    }
}

void waitq_block(waitq_t *q, spinlock_t *lock, uint64_t irqflags, uint64_t deadline_ns)
{
    task_t *me = syscall_cur_task();
    if (!me) {
        spinlock_release_irqrestore(lock, irqflags);
        task_yield();
        return;
    }
    uint64_t wake = deadline_ns;
    if (!waitq_add(q, me)) {
        uint64_t poll = sched_now_ns() + 1000000ULL;
        if (!wake || poll < wake) wake = poll;
    }
    me->wakeup_time_ns = wake;
    me->runnable = false;
    me->state    = TASK_BLOCKED;
    if (wake) sched_note_wakeup(wake);
    spinlock_release_irqrestore(lock, irqflags);
    sched_reschedule();
    me->wakeup_time_ns = 0;
    uint64_t f = spinlock_acquire_irqsave(lock);
    waitq_del(q, me);
    spinlock_release_irqrestore(lock, f);
}
