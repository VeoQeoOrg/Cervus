#include "../../include/time/ktimer.h"
#include "../../include/sched/spinlock.h"
#include "../../include/sched/sched.h"
#include "../../include/drivers/timer.h"

extern int64_t clock_realtime_offset_ns(void);

static spinlock_t g_kt_lock = SPINLOCK_INIT;
static ktimer_t  *g_kt_head;
static volatile uint64_t g_kt_next = UINT64_MAX;

int64_t ktimer_realtime_offset(void)
{
    return clock_realtime_offset_ns();
}

static void unlink_locked(ktimer_t *t)
{
    ktimer_t **pp = &g_kt_head;
    while (*pp && *pp != t) pp = &(*pp)->next;
    if (*pp) *pp = t->next;
    t->next = NULL;
    t->queued = 0;
}

static void insert_locked(ktimer_t *t)
{
    ktimer_t **pp = &g_kt_head;
    while (*pp && (*pp)->expires <= t->expires) pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
    t->queued = 1;
}

static void update_next_locked(void)
{
    __atomic_store_n(&g_kt_next, g_kt_head ? g_kt_head->expires : UINT64_MAX, __ATOMIC_RELEASE);
}

void ktimer_init(ktimer_t *t, void (*fn)(ktimer_t *))
{
    t->next = NULL;
    t->expires = 0;
    t->rt_expires = 0;
    t->base = KTIMER_MONOTONIC;
    t->queued = 0;
    t->running = 0;
    t->fn = fn;
}

static void arm(ktimer_t *t, uint64_t expires, int base, int64_t rt_expires)
{
    uint64_t f = spinlock_acquire_irqsave(&g_kt_lock);
    if (t->queued) unlink_locked(t);
    t->expires = expires;
    t->base = base;
    t->rt_expires = rt_expires;
    insert_locked(t);
    update_next_locked();
    spinlock_release_irqrestore(&g_kt_lock, f);
}

void ktimer_arm(ktimer_t *t, uint64_t expires_mono_ns)
{
    arm(t, expires_mono_ns, KTIMER_MONOTONIC, 0);
}

static uint64_t rt_to_mono(int64_t real_ns)
{
    int64_t mono = real_ns - ktimer_realtime_offset();
    return mono < 0 ? 0 : (uint64_t)mono;
}

void ktimer_arm_realtime(ktimer_t *t, int64_t expires_real_ns)
{
    arm(t, rt_to_mono(expires_real_ns), KTIMER_REALTIME, expires_real_ns);
}

bool ktimer_cancel(ktimer_t *t)
{
    bool was = false;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&g_kt_lock);
        if (t->queued) {
            unlink_locked(t);
            update_next_locked();
            was = true;
        }
        int running = t->running;
        spinlock_release_irqrestore(&g_kt_lock, f);
        if (!running) return was;
        while (__atomic_load_n(&t->running, __ATOMIC_ACQUIRE))
            __asm__ volatile("pause");
    }
}

void ktimer_run(uint64_t now_ns)
{
    if (now_ns < __atomic_load_n(&g_kt_next, __ATOMIC_ACQUIRE)) return;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&g_kt_lock);
        ktimer_t *t = g_kt_head;
        if (!t || t->expires > now_ns) {
            update_next_locked();
            spinlock_release_irqrestore(&g_kt_lock, f);
            return;
        }
        g_kt_head = t->next;
        t->next = NULL;
        t->queued = 0;
        __atomic_store_n(&t->running, 1, __ATOMIC_RELEASE);
        update_next_locked();
        spinlock_release_irqrestore(&g_kt_lock, f);

        t->fn(t);
        __atomic_store_n(&t->running, 0, __ATOMIC_RELEASE);
    }
}

void ktimer_clock_was_set(void)
{
    uint64_t f = spinlock_acquire_irqsave(&g_kt_lock);
    ktimer_t *moved = NULL;
    ktimer_t **pp = &g_kt_head;
    while (*pp) {
        ktimer_t *t = *pp;
        if (t->base == KTIMER_REALTIME) {
            *pp = t->next;
            t->next = moved;
            moved = t;
        } else {
            pp = &t->next;
        }
    }
    while (moved) {
        ktimer_t *t = moved;
        moved = t->next;
        t->expires = rt_to_mono(t->rt_expires);
        insert_locked(t);
    }
    update_next_locked();
    spinlock_release_irqrestore(&g_kt_lock, f);
}
