#include "../../include/time/ptimer.h"
#include "../../include/time/ktimer.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/sched/spinlock.h"
#include "../../include/sched/kmutex.h"
#include "../../include/drivers/timer.h"
#include <stdlib.h>
#include <string.h>

#define PTIMER_MAX 256

#define SIGEV_SIGNAL_K    0
#define SIGEV_NONE_K      1
#define SIGEV_THREAD_ID_K 4

#define TIMER_ABSTIME_K 1

typedef struct ptimer_table ptimer_table_t;

typedef struct ptimer {
    ktimer_t        kt;
    ptimer_table_t *tab;
    int             id;
    int             realtime;
    int             notify;
    int             signo;
    uint64_t        value;
    uint32_t        tid;
    uint64_t        interval;
    int             armed;
    int             pending;
    int             overrun;
    int             overrun_last;
} ptimer_t;

struct ptimer_table {
    kmutex_t   op;
    spinlock_t lock;
    task_t    *leader;
    ptimer_t  *slot[PTIMER_MAX];
};

typedef struct {
    uint64_t value;
    int32_t  signo;
    int32_t  notify;
    int32_t  tid;
} user_sigevent_t;

typedef struct {
    int64_t sec;
    int64_t nsec;
} user_timespec_t;

typedef struct {
    user_timespec_t interval;
    user_timespec_t value;
} user_itimerspec_t;

static spinlock_t g_tab_lock = SPINLOCK_INIT;

static int clock_class(uint64_t clock)
{
    switch (clock) {
    case 0: case 8: case 11: return 1;
    case 1: case 7: case 9:  return 0;
    default:                 return -1;
    }
}

static int add_overrun(int a, uint64_t b)
{
    uint64_t s = (uint64_t)a + b;
    return s > 0x7fffffff ? 0x7fffffff : (int)s;
}

static void ptimer_fire(ktimer_t *kt)
{
    ptimer_t *p = (ptimer_t *)kt;
    ptimer_table_t *tab = p->tab;
    uint64_t now = sched_now_ns();

    uint64_t f = spinlock_acquire_irqsave(&tab->lock);
    if (!p->armed) {
        spinlock_release_irqrestore(&tab->lock, f);
        return;
    }
    if (p->interval) {
        uint64_t missed = now > kt->expires ? (now - kt->expires) / p->interval : 0;
        p->overrun = add_overrun(p->overrun, missed);
        uint64_t step = (missed + 1) * p->interval;
        if (kt->base == KTIMER_REALTIME) ktimer_arm_realtime(kt, kt->rt_expires + (int64_t)step);
        else ktimer_arm(kt, kt->expires + step);
    } else {
        p->armed = 0;
    }
    if (p->notify == SIGEV_NONE_K) {
        spinlock_release_irqrestore(&tab->lock, f);
        return;
    }
    if (p->pending) {
        p->overrun = add_overrun(p->overrun, 1);
        spinlock_release_irqrestore(&tab->lock, f);
        return;
    }
    p->pending = 1;
    int sig = p->signo, id = p->id, notify = p->notify;
    uint32_t tid = p->tid;
    uint64_t value = p->value;
    spinlock_release_irqrestore(&tab->lock, f);

    task_t *target = notify == SIGEV_THREAD_ID_K ? task_group_find(tab->leader, tid)
                                                 : task_group_pick(tab->leader, sig);
    ksiginfo_t si = { .code = SI_TIMER_CODE, .pid = (uint32_t)id, .value = value };
    int r = target ? signal_post_timer(target, sig, &si) : 0;
    if (r != 1) {
        f = spinlock_acquire_irqsave(&tab->lock);
        p->pending = 0;
        if (r < 0) p->overrun = add_overrun(p->overrun, 1);
        spinlock_release_irqrestore(&tab->lock, f);
    }
}

static ptimer_table_t *table_of(task_t *t, bool create)
{
    task_t *leader = task_leader(t);
    uint64_t f = spinlock_acquire_irqsave(&g_tab_lock);
    ptimer_table_t *tab = leader->ptimers;
    spinlock_release_irqrestore(&g_tab_lock, f);
    if (tab || !create) return tab;

    ptimer_table_t *fresh = calloc(1, sizeof(*fresh));
    if (!fresh) return NULL;
    kmutex_init(&fresh->op);
    fresh->leader = leader;
    f = spinlock_acquire_irqsave(&g_tab_lock);
    if (!leader->ptimers) leader->ptimers = fresh;
    else { tab = leader->ptimers; }
    spinlock_release_irqrestore(&g_tab_lock, f);
    if (tab) { free(fresh); return tab; }
    return fresh;
}

static ptimer_t *lookup(task_t *t, uint64_t id, ptimer_table_t **tabp)
{
    *tabp = NULL;
    if (id >= PTIMER_MAX) return NULL;
    ptimer_table_t *tab = table_of(t, false);
    if (!tab) return NULL;
    kmutex_lock(&tab->op);
    ptimer_t *p = tab->slot[id];
    if (!p) { kmutex_unlock(&tab->op); return NULL; }
    *tabp = tab;
    return p;
}

int64_t sys_timer_create(uint64_t clock, uint64_t sevp, uint64_t idp)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    int realtime = clock_class(clock);
    if (realtime < 0) return -EINVAL;
    if (!idp) return -EFAULT;

    user_sigevent_t sev = { .signo = SIGALRM, .notify = SIGEV_SIGNAL_K };
    bool have = false;
    if (sevp) {
        if (syscall_copy_from_user(&sev, (const void *)sevp, sizeof sev) < 0) return -EFAULT;
        have = true;
    }
    if (sev.notify != SIGEV_SIGNAL_K && sev.notify != SIGEV_NONE_K && sev.notify != SIGEV_THREAD_ID_K)
        return -EINVAL;
    if (sev.notify != SIGEV_NONE_K && (sev.signo <= 0 || sev.signo >= NSIG)) return -EINVAL;
    if (sev.notify == SIGEV_THREAD_ID_K && !task_group_find(task_leader(me), (uint32_t)sev.tid))
        return -EINVAL;

    ptimer_table_t *tab = table_of(me, true);
    if (!tab) return -ENOMEM;
    ptimer_t *p = calloc(1, sizeof(*p));
    if (!p) return -ENOMEM;
    ktimer_init(&p->kt, ptimer_fire);
    p->tab = tab;
    p->realtime = realtime;
    p->notify = sev.notify;
    p->signo = sev.signo;
    p->tid = (uint32_t)sev.tid;

    kmutex_lock(&tab->op);
    uint64_t f = spinlock_acquire_irqsave(&tab->lock);
    int id = -1;
    for (int i = 0; i < PTIMER_MAX; i++)
        if (!tab->slot[i]) { id = i; break; }
    if (id >= 0) {
        p->id = id;
        p->value = have ? sev.value : (uint64_t)id;
        tab->slot[id] = p;
    }
    spinlock_release_irqrestore(&tab->lock, f);
    if (id < 0) { kmutex_unlock(&tab->op); free(p); return -EAGAIN; }

    int32_t uid = id;
    if (syscall_copy_to_user((void *)idp, &uid, sizeof uid) < 0) {
        f = spinlock_acquire_irqsave(&tab->lock);
        tab->slot[id] = NULL;
        spinlock_release_irqrestore(&tab->lock, f);
        kmutex_unlock(&tab->op);
        free(p);
        return -EFAULT;
    }
    kmutex_unlock(&tab->op);
    return 0;
}

static uint64_t ts_ns(const user_timespec_t *ts)
{
    return (uint64_t)ts->sec * 1000000000ULL + (uint64_t)ts->nsec;
}

static void ns_ts(uint64_t ns, user_timespec_t *ts)
{
    ts->sec = (int64_t)(ns / 1000000000ULL);
    ts->nsec = (int64_t)(ns % 1000000000ULL);
}

static void current_value(ptimer_t *p, user_itimerspec_t *out)
{
    uint64_t now = sched_now_ns();
    uint64_t left = 0;
    if (p->armed) left = p->kt.expires > now ? p->kt.expires - now : 1;
    ns_ts(p->interval, &out->interval);
    ns_ts(left, &out->value);
}

static bool ts_valid(const user_timespec_t *ts)
{
    return ts->sec >= 0 && ts->nsec >= 0 && ts->nsec < 1000000000LL;
}

int64_t sys_timer_settime(uint64_t id, uint64_t flags, uint64_t newp, uint64_t oldp)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    if (!newp) return -EFAULT;
    user_itimerspec_t nv;
    if (syscall_copy_from_user(&nv, (const void *)newp, sizeof nv) < 0) return -EFAULT;
    if (!ts_valid(&nv.interval) || !ts_valid(&nv.value)) return -EINVAL;

    ptimer_table_t *tab;
    ptimer_t *p = lookup(me, id, &tab);
    if (!p) return -EINVAL;

    uint64_t f = spinlock_acquire_irqsave(&tab->lock);
    user_itimerspec_t ov;
    current_value(p, &ov);
    p->armed = 0;
    spinlock_release_irqrestore(&tab->lock, f);
    ktimer_cancel(&p->kt);

    uint64_t value = ts_ns(&nv.value);
    f = spinlock_acquire_irqsave(&tab->lock);
    p->interval = value ? ts_ns(&nv.interval) : 0;
    p->overrun = 0;
    if (value) {
        p->armed = 1;
        if (!(flags & TIMER_ABSTIME_K)) ktimer_arm(&p->kt, sched_now_ns() + value);
        else if (p->realtime) ktimer_arm_realtime(&p->kt, (int64_t)value);
        else ktimer_arm(&p->kt, value);
    }
    spinlock_release_irqrestore(&tab->lock, f);
    kmutex_unlock(&tab->op);

    if (oldp && syscall_copy_to_user((void *)oldp, &ov, sizeof ov) < 0) return -EFAULT;
    return 0;
}

int64_t sys_timer_gettime(uint64_t id, uint64_t curp)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    ptimer_table_t *tab;
    ptimer_t *p = lookup(me, id, &tab);
    if (!p) return -EINVAL;
    user_itimerspec_t cv;
    uint64_t f = spinlock_acquire_irqsave(&tab->lock);
    current_value(p, &cv);
    spinlock_release_irqrestore(&tab->lock, f);
    kmutex_unlock(&tab->op);
    if (syscall_copy_to_user((void *)curp, &cv, sizeof cv) < 0) return -EFAULT;
    return 0;
}

int64_t sys_timer_getoverrun(uint64_t id)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    ptimer_table_t *tab;
    ptimer_t *p = lookup(me, id, &tab);
    if (!p) return -EINVAL;
    int n = p->overrun_last;
    kmutex_unlock(&tab->op);
    return n;
}

static void destroy(ptimer_t *p)
{
    uint64_t f = spinlock_acquire_irqsave(&p->tab->lock);
    p->armed = 0;
    spinlock_release_irqrestore(&p->tab->lock, f);
    ktimer_cancel(&p->kt);
    free(p);
}

int64_t sys_timer_delete(uint64_t id)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    if (id >= PTIMER_MAX) return -EINVAL;
    ptimer_table_t *tab = table_of(me, false);
    if (!tab) return -EINVAL;
    kmutex_lock(&tab->op);
    uint64_t f = spinlock_acquire_irqsave(&tab->lock);
    ptimer_t *p = tab->slot[id];
    tab->slot[id] = NULL;
    spinlock_release_irqrestore(&tab->lock, f);
    if (p) destroy(p);
    kmutex_unlock(&tab->op);
    return p ? 0 : -EINVAL;
}

void ptimer_group_exit(task_t *leader)
{
    uint64_t f = spinlock_acquire_irqsave(&g_tab_lock);
    ptimer_table_t *tab = leader->ptimers;
    leader->ptimers = NULL;
    spinlock_release_irqrestore(&g_tab_lock, f);
    if (!tab) return;
    for (int i = 0; i < PTIMER_MAX; i++) {
        ptimer_t *p = tab->slot[i];
        tab->slot[i] = NULL;
        if (p) destroy(p);
    }
    free(tab);
}

static ptimer_table_t *table_locked(task_t *t, uint64_t *flags)
{
    *flags = spinlock_acquire_irqsave(&g_tab_lock);
    ptimer_table_t *tab = task_leader(t)->ptimers;
    if (!tab) spinlock_release_irqrestore(&g_tab_lock, *flags);
    return tab;
}

void ptimer_signal_taken(task_t *t, ksiginfo_t *si)
{
    uint64_t gf;
    ptimer_table_t *tab = table_locked(t, &gf);
    si->uid = 0;
    if (!tab) return;
    if (si->pid < PTIMER_MAX) {
        uint64_t f = spinlock_acquire_irqsave(&tab->lock);
        ptimer_t *p = tab->slot[si->pid];
        if (p) {
            p->overrun_last = p->overrun;
            p->overrun = 0;
            p->pending = 0;
            si->uid = (uint32_t)p->overrun_last;
        }
        spinlock_release_irqrestore(&tab->lock, f);
    }
    spinlock_release_irqrestore(&g_tab_lock, gf);
}

void ptimer_signal_dropped(task_t *t, int timerid)
{
    ksiginfo_t si = { .code = SI_TIMER_CODE, .pid = (uint32_t)timerid };
    ptimer_signal_taken(t, &si);
}
