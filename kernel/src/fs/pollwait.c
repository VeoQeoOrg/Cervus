#include "../../include/fs/pollwait.h"
#include "../../include/fs/vfs.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/spinlock.h"
#include "../../include/syscall/syscall_internal.h"
#include <stdlib.h>
#include <string.h>

#define POLLWAIT_SLOTS   512
#define POLL_FALLBACK_NS 20000000ULL

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

static poll_table_t *cur_table(void) {
    task_t *me = syscall_cur_task();
    return me ? (poll_table_t *)me->poll_tbl : NULL;
}

static void put_file(void *obj) {
    vfs_file_free((vfs_file_t *)obj);
}

static int table_grow(poll_table_t *pt) {
    int ncap = pt->cap * 2;
    poll_entry_t *ne = malloc((size_t)ncap * sizeof *ne);
    if (!ne) return 0;
    memcpy(ne, pt->e, (size_t)pt->n * sizeof *ne);
    if (pt->e != pt->inl) free(pt->e);
    pt->e = ne;
    pt->cap = ncap;
    return 1;
}

void poll_wait_obj(waitq_t *q, void (*put)(void *), void *obj) {
    poll_table_t *pt = cur_table();
    if (!pt) {
        if (put) put(obj);
        return;
    }
    pt->touched = 1;
    if (pt->qproc) {
        pt->qproc(pt, q, put, obj);
        return;
    }
    if (pt->noreg) {
        if (put) put(obj);
        return;
    }
    if (pt->n == pt->cap && !table_grow(pt)) {
        pt->legacy = 1;
        if (put) put(obj);
        return;
    }
    poll_entry_t *e = &pt->e[pt->n++];
    e->q = q;
    e->put = put;
    e->obj = obj;
    if (!waitq_poll_add(q, syscall_cur_task())) pt->legacy = 1;
}

void poll_wait(waitq_t *q) {
    poll_table_t *pt = cur_table();
    if (!pt) return;
    if (pt->qproc || pt->noreg) {
        poll_wait_obj(q, NULL, NULL);
        return;
    }
    vfs_file_t *file = pt->cur_file;
    if (file) __atomic_fetch_add(&file->refcount, 1, __ATOMIC_RELAXED);
    poll_wait_obj(q, file ? put_file : NULL, file);
}

void poll_deadline(uint64_t deadline_ns) {
    poll_table_t *pt = cur_table();
    if (!pt) return;
    pt->touched = 1;
    if (deadline_ns && (!pt->deadline_ns || deadline_ns < pt->deadline_ns))
        pt->deadline_ns = deadline_ns;
}

void poll_mark_legacy(void) {
    poll_table_t *pt = cur_table();
    if (!pt) return;
    pt->touched = 1;
    pt->legacy = 1;
}

void poll_table_init(poll_table_t *pt) {
    memset(pt, 0, sizeof *pt);
    pt->e = pt->inl;
    pt->cap = (int)(sizeof pt->inl / sizeof pt->inl[0]);
}

uint64_t poll_table_begin(poll_table_t *pt) {
    task_t *me = syscall_cur_task();
    pt->touched = 0;
    pt->legacy = 0;
    pt->deadline_ns = 0;
    pt->cur_file = NULL;
    if (me) me->poll_tbl = pt;
    return me ? __atomic_load_n(&me->poll_wake_seq, __ATOMIC_SEQ_CST) : 0;
}

void poll_table_end(poll_table_t *pt) {
    (void)pt;
    task_t *me = syscall_cur_task();
    if (me) me->poll_tbl = NULL;
}

vfs_file_t *poll_table_file(vfs_file_t *file) {
    poll_table_t *pt = cur_table();
    if (!pt) return NULL;
    vfs_file_t *prev = pt->cur_file;
    pt->cur_file = file;
    pt->touched = 0;
    return prev;
}

void poll_table_note(vfs_file_t *prev) {
    poll_table_t *pt = cur_table();
    if (!pt) return;
    if (!pt->touched) pt->legacy = 1;
    pt->cur_file = prev;
    pt->touched = 1;
}

void poll_table_release(poll_table_t *pt) {
    task_t *me = syscall_cur_task();
    for (int i = 0; i < pt->n; i++) {
        poll_entry_t *e = &pt->e[i];
        if (me) waitq_poll_del(e->q, me);
        if (e->put) e->put(e->obj);
    }
    pt->n = 0;
    if (pt->e != pt->inl) {
        free(pt->e);
        pt->e = pt->inl;
        pt->cap = (int)(sizeof pt->inl / sizeof pt->inl[0]);
    }
}

void poll_table_block(poll_table_t *pt, uint64_t gseen, uint64_t wseen, uint64_t deadline_ns) {
    task_t *me = syscall_cur_task();
    if (!me) return;
    uint64_t dl = deadline_ns;
    if (pt->deadline_ns && (!dl || pt->deadline_ns < dl)) dl = pt->deadline_ns;

    int slot = -1;
    uint64_t f = 0;
    if (pt->legacy) {
        uint64_t fb = sched_now_ns() + POLL_FALLBACK_NS;
        if (!dl || fb < dl) dl = fb;
        f = spinlock_acquire_irqsave(&g_lock);
        if (__atomic_load_n(&g_seq, __ATOMIC_ACQUIRE) != gseen) {
            spinlock_release_irqrestore(&g_lock, f);
            return;
        }
        for (int i = 0; i < POLLWAIT_SLOTS; i++)
            if (!g_slots[i]) { slot = i; break; }
        if (slot < 0) {
            spinlock_release_irqrestore(&g_lock, f);
            task_sleep_ns(1000000ULL);
            return;
        }
        g_slots[slot] = me;
        __atomic_add_fetch(&g_nwait, 1, __ATOMIC_ACQ_REL);
    } else {
        asm volatile("cli");
    }

    me->wakeup_time_ns = dl;
    me->runnable = false;
    me->state = TASK_BLOCKED;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (__atomic_load_n(&me->poll_wake_seq, __ATOMIC_SEQ_CST) != wseen) task_unblock(me);
    else if (dl) sched_note_wakeup(dl);

    if (pt->legacy) spinlock_release_irqrestore(&g_lock, f);
    sched_reschedule();
    me->wakeup_time_ns = 0;

    if (slot >= 0) {
        f = spinlock_acquire_irqsave(&g_lock);
        if (g_slots[slot] == me) g_slots[slot] = NULL;
        __atomic_sub_fetch(&g_nwait, 1, __ATOMIC_ACQ_REL);
        spinlock_release_irqrestore(&g_lock, f);
    }
}
