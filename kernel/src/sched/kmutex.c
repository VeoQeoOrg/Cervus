#include "../../include/sched/kmutex.h"
#include "../../include/sched/sched.h"
#include "../../include/smp/percpu.h"
#include "../../include/smp/smp.h"

typedef struct kmutex_waiter {
    task_t               *task;
    struct kmutex_waiter *next;
    volatile int          granted;
} kmutex_waiter_t;

static task_t *me(void) {
    return get_percpu() ? (task_t *)percpu_current_task() : current_task[smp_cpu_index()];
}

void kmutex_init(kmutex_t *m) {
    m->locked = 0;
    m->owner = 0;
    m->wait_lock = (spinlock_t)SPINLOCK_INIT;
    m->head = m->tail = 0;
}

bool kmutex_trylock(kmutex_t *m) {
    int expected = 0;
    if (__atomic_compare_exchange_n(&m->locked, &expected, 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        m->owner = me();
        return true;
    }
    return false;
}

void kmutex_lock(kmutex_t *m) {
    if (kmutex_trylock(m)) return;
    task_t *t = me();
    bool can_sleep = get_percpu() && t && !t->irq_nest && !task_is_idle(t) && spin_depth() == 0;
    if (!can_sleep) {
        while (!kmutex_trylock(m)) {
            tlb_poll();
            asm volatile("pause");
        }
        return;
    }
    kmutex_waiter_t w = { t, 0, 0 };
    uint64_t f = spinlock_acquire_irqsave(&m->wait_lock);
    if (kmutex_trylock(m)) {
        spinlock_release_irqrestore(&m->wait_lock, f);
        return;
    }
    if (m->tail) m->tail->next = &w; else m->head = &w;
    m->tail = &w;
    t->runnable = false;
    t->state = TASK_BLOCKED;
    t->sleep_noint++;
    spinlock_release_irqrestore(&m->wait_lock, f);
    while (!__atomic_load_n(&w.granted, __ATOMIC_ACQUIRE)) {
        sched_reschedule();
        if (!__atomic_load_n(&w.granted, __ATOMIC_ACQUIRE)) {
            uint64_t g = spinlock_acquire_irqsave(&m->wait_lock);
            if (!w.granted) {
                t->runnable = false;
                t->state = TASK_BLOCKED;
            }
            spinlock_release_irqrestore(&m->wait_lock, g);
        }
    }
    t->sleep_noint--;
    m->owner = t;
}

void kmutex_unlock(kmutex_t *m) {
    uint64_t f = spinlock_acquire_irqsave(&m->wait_lock);
    kmutex_waiter_t *w = m->head;
    m->owner = 0;
    if (w) {
        m->head = w->next;
        if (!m->head) m->tail = 0;
        task_t *t = w->task;
        __atomic_store_n(&w->granted, 1, __ATOMIC_RELEASE);
        spinlock_release_irqrestore(&m->wait_lock, f);
        task_unblock(t);
        return;
    }
    __atomic_store_n(&m->locked, 0, __ATOMIC_RELEASE);
    spinlock_release_irqrestore(&m->wait_lock, f);
}

bool kmutex_held_by_me(kmutex_t *m) {
    return m->locked && m->owner == me();
}
