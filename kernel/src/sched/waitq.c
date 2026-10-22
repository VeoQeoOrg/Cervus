#include "../../include/sched/waitq.h"
#include "../../include/fs/pollwait.h"
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

int waitq_poll_add(waitq_t *q, task_t *t)
{
    int ok = 0;
    uint64_t f = spinlock_acquire_irqsave(&q->plock);
    for (int i = 0; i < WAITQ_POLLERS; i++)
        if (q->poller[i] == t) { ok = 1; break; }
    for (int i = 0; !ok && i < WAITQ_POLLERS; i++)
        if (!q->poller[i]) { q->poller[i] = t; ok = 1; }
    spinlock_release_irqrestore(&q->plock, f);
    return ok;
}

void waitq_poll_del(waitq_t *q, task_t *t)
{
    uint64_t f = spinlock_acquire_irqsave(&q->plock);
    for (int i = 0; i < WAITQ_POLLERS; i++)
        if (q->poller[i] == t) q->poller[i] = NULL;
    spinlock_release_irqrestore(&q->plock, f);
}

int waitq_cb_add(waitq_t *q, waitq_cb_t *cb)
{
    int ok = 0;
    uint64_t f = spinlock_acquire_irqsave(&q->plock);
    for (int i = 0; i < WAITQ_CBS; i++)
        if (!q->cb[i]) { q->cb[i] = cb; ok = 1; break; }
    spinlock_release_irqrestore(&q->plock, f);
    return ok;
}

void waitq_cb_del(waitq_t *q, waitq_cb_t *cb)
{
    uint64_t f = spinlock_acquire_irqsave(&q->plock);
    for (int i = 0; i < WAITQ_CBS; i++)
        if (q->cb[i] == cb) q->cb[i] = NULL;
    spinlock_release_irqrestore(&q->plock, f);
}

void waitq_wake_pollers(waitq_t *q)
{
    uint64_t f = spinlock_acquire_irqsave(&q->plock);
    for (int i = 0; i < WAITQ_POLLERS; i++) {
        task_t *t = q->poller[i];
        if (!t) continue;
        q->poller[i] = NULL;
        __atomic_add_fetch(&t->poll_wake_seq, 1, __ATOMIC_SEQ_CST);
        task_unblock(t);
    }
    for (int i = 0; i < WAITQ_CBS; i++)
        if (q->cb[i]) q->cb[i]->fn(q->cb[i]);
    spinlock_release_irqrestore(&q->plock, f);
}

void waitq_wake_all(waitq_t *q)
{
    for (int i = 0; i < WAITQ_SLOTS; i++) {
        task_t *w = q->slot[i];
        if (!w) continue;
        q->slot[i] = NULL;
        task_unblock(w);
    }
    waitq_wake_pollers(q);
    poll_notify();
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
