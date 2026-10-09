#include "../../include/ipc/ipc.h"
#include "../../include/memory/pmm.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/waitq.h"
#include "../../include/signal/signal.h"
#include "../../include/drivers/timer.h"
#include "../../include/syscall/syscall_internal.h"
#include <string.h>

#define SEMMSL   250
#define SEMOPM   500
#define SEMVMX   32767
#define SEM_UNDO 0x1000

#define GETPID   11
#define GETVAL   12
#define GETALL   13
#define GETNCNT  14
#define GETZCNT  15
#define SETVAL   16
#define SETALL   17

struct k_sembuf { uint16_t num; int16_t op; int16_t flg; };

typedef struct sem_undo sem_undo_t;
typedef struct semset semset_t;

struct sem_undo {
    sem_undo_t *pnext;
    sem_undo_t *snext;
    semset_t   *set;
    int         nsems;
    int16_t     adj[];
};

typedef struct {
    int      val;
    uint32_t pid;
    int      ncnt, zcnt;
} ksem_t;

struct semset {
    ipc_obj_t   o;
    int         nsems;
    int64_t     otime, ctime;
    waitq_t     wq;
    sem_undo_t *undos;
    ksem_t      sem[];
};

typedef struct {
    ipc_perm_user_t perm;
    int64_t  otime;
    uint64_t unused1;
    int64_t  ctime;
    uint64_t unused2;
    uint64_t nsems;
    uint64_t unused3, unused4;
} semid_ds_user_t;

static void set_destroy(ipc_obj_t *o)
{
    semset_t *s = (semset_t *)o;
    for (sem_undo_t *u = s->undos; u; u = u->snext) u->set = NULL;
    kfree(s);
}

static ipc_ns_t g_ns = { .lock = SPINLOCK_INIT, .destroy = set_destroy };

static ipc_obj_t *set_create(void *arg, int *err)
{
    int nsems = (int)(intptr_t)arg;
    if (nsems <= 0 || nsems > SEMMSL) { *err = -EINVAL; return NULL; }
    semset_t *s = kzalloc(sizeof *s + (size_t)nsems * sizeof(ksem_t));
    if (!s) return NULL;
    s->nsems = nsems;
    s->ctime = ipc_now();
    return &s->o;
}

static int set_check(ipc_obj_t *o, void *arg)
{
    return (int)(intptr_t)arg > ((semset_t *)o)->nsems ? -EINVAL : 0;
}

int64_t sys_semget(uint64_t key, uint64_t nsems, uint64_t flags)
{
    if ((int64_t)nsems < 0 || nsems > SEMMSL) return -EINVAL;
    return ipc_get(&g_ns, (int32_t)key, (int)flags, set_create, set_check, (void *)(intptr_t)nsems);
}

static sem_undo_t *undo_for(semset_t *s, task_t *leader)
{
    for (sem_undo_t *u = leader->sem_undo; u; u = u->pnext)
        if (u->set == s) return u;
    sem_undo_t *u = kzalloc(sizeof *u + (size_t)s->nsems * sizeof(int16_t));
    if (!u) return NULL;
    u->set = s;
    u->nsems = s->nsems;
    u->pnext = leader->sem_undo;
    leader->sem_undo = u;
    u->snext = s->undos;
    s->undos = u;
    return u;
}

static void clear_undo(semset_t *s, int num)
{
    for (sem_undo_t *u = s->undos; u; u = u->snext) {
        if (num < 0) memset(u->adj, 0, (size_t)u->nsems * sizeof(int16_t));
        else if (num < u->nsems) u->adj[num] = 0;
    }
}

static int try_ops(semset_t *s, struct k_sembuf *ops, size_t n, task_t *me, int *block)
{
    int tmp[SEMMSL];
    for (size_t i = 0; i < n; i++) tmp[ops[i].num] = s->sem[ops[i].num].val;
    for (size_t i = 0; i < n; i++) {
        int num = ops[i].num, op = ops[i].op;
        int v = tmp[num];
        if (op > 0) {
            if (v + op > SEMVMX) return -ERANGE;
            tmp[num] = v + op;
        } else if (op == 0) {
            if (v != 0) { *block = (int)i; return 1; }
        } else {
            if (v + op < 0) { *block = (int)i; return 1; }
            tmp[num] = v + op;
        }
    }
    task_t *leader = task_leader(me);
    for (size_t i = 0; i < n; i++) {
        if (!(ops[i].flg & SEM_UNDO) || !ops[i].op) continue;
        sem_undo_t *u = undo_for(s, leader);
        if (!u) return -ENOMEM;
    }
    for (size_t i = 0; i < n; i++) {
        int num = ops[i].num;
        s->sem[num].val = tmp[num];
        s->sem[num].pid = task_tgid(me);
        if ((ops[i].flg & SEM_UNDO) && ops[i].op) undo_for(s, leader)->adj[num] -= ops[i].op;
    }
    s->otime = ipc_now();
    waitq_wake_all(&s->wq);
    return 0;
}

int64_t sys_semtimedop(uint64_t id, uint64_t uops, uint64_t nsops, uint64_t uts)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    if (nsops == 0 || nsops > SEMOPM) return -EINVAL;
    struct k_sembuf *ops = kmalloc(nsops * sizeof *ops);
    if (!ops) return -ENOMEM;
    if (syscall_copy_from_user(ops, (const void *)uops, nsops * sizeof *ops) < 0) { kfree(ops); return -EFAULT; }
    uint64_t deadline = 0;
    if (uts) {
        int64_t ts[2];
        if (syscall_copy_from_user(ts, (const void *)uts, sizeof ts) < 0) { kfree(ops); return -EFAULT; }
        if (ts[0] < 0 || ts[1] < 0 || ts[1] >= 1000000000) { kfree(ops); return -EINVAL; }
        deadline = sched_now_ns() + (uint64_t)ts[0] * 1000000000ULL + (uint64_t)ts[1];
    }
    bool alter = false;
    for (uint64_t i = 0; i < nsops; i++) if (ops[i].op) alter = true;

    int64_t r;
    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    for (;;) {
        semset_t *s = (semset_t *)ipc_lookup_locked(&g_ns, (int)id);
        if (!s) { r = -EINVAL; break; }
        bool big = false;
        for (uint64_t i = 0; i < nsops; i++) if (ops[i].num >= s->nsems) big = true;
        if (big) { r = -EFBIG; break; }
        if (!ipc_perm_ok(&s->o, me, alter ? IPC_W : IPC_R)) { r = -EACCES; break; }
        int block = -1;
        int t = try_ops(s, ops, nsops, me, &block);
        if (t <= 0) { r = t; break; }
        if (ops[block].flg & IPC_NOWAIT_K) { r = -EAGAIN; break; }
        if (deadline && sched_now_ns() >= deadline) { r = -EAGAIN; break; }
        if (me->pending_kill || signal_pending_deliverable(me)) { r = -EINTR; break; }
        int num = ops[block].num;
        bool zero = ops[block].op == 0;
        if (zero) s->sem[num].zcnt++; else s->sem[num].ncnt++;
        s->o.refs++;
        waitq_block(&s->wq, &g_ns.lock, f, deadline);
        f = spinlock_acquire_irqsave(&g_ns.lock);
        bool gone = s->o.dead;
        if (!gone) { if (zero) s->sem[num].zcnt--; else s->sem[num].ncnt--; }
        ipc_put_locked(&g_ns, &s->o);
        if (gone) { r = -EIDRM; break; }
    }
    spinlock_release_irqrestore(&g_ns.lock, f);
    kfree(ops);
    return r;
}

int64_t sys_semop(uint64_t id, uint64_t uops, uint64_t nsops)
{
    return sys_semtimedop(id, uops, nsops, 0);
}

int64_t sys_semctl(uint64_t id, uint64_t num, uint64_t cmd, uint64_t arg)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    cmd &= ~0x100ULL;
    semid_ds_user_t ds;
    uint16_t vals[SEMMSL];
    if (cmd == IPC_SET_K && syscall_copy_from_user(&ds, (const void *)arg, sizeof ds) < 0) return -EFAULT;
    if (cmd == SETALL) {
        uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
        semset_t *s = (semset_t *)ipc_lookup_locked(&g_ns, (int)id);
        int n = s ? s->nsems : 0;
        spinlock_release_irqrestore(&g_ns.lock, f);
        if (!s) return -EINVAL;
        if (syscall_copy_from_user(vals, (const void *)arg, (size_t)n * sizeof(uint16_t)) < 0) return -EFAULT;
    }

    int64_t r = 0;
    int nout = 0;
    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    semset_t *s = (semset_t *)ipc_lookup_locked(&g_ns, (int)id);
    if (!s) { spinlock_release_irqrestore(&g_ns.lock, f); return -EINVAL; }
    bool one = cmd == GETPID || cmd == GETVAL || cmd == GETNCNT || cmd == GETZCNT || cmd == SETVAL;
    if (one && num >= (uint64_t)s->nsems) { spinlock_release_irqrestore(&g_ns.lock, f); return -EINVAL; }
    switch (cmd) {
    case GETPID:
    case GETVAL:
    case GETNCNT:
    case GETZCNT:
    case GETALL:
    case IPC_STAT_K:
        if (!ipc_perm_ok(&s->o, me, IPC_R)) { r = -EACCES; break; }
        if (cmd == GETPID) r = s->sem[num].pid;
        else if (cmd == GETVAL) r = s->sem[num].val;
        else if (cmd == GETNCNT) r = s->sem[num].ncnt;
        else if (cmd == GETZCNT) r = s->sem[num].zcnt;
        else if (cmd == GETALL) {
            nout = s->nsems;
            for (int i = 0; i < nout; i++) vals[i] = (uint16_t)s->sem[i].val;
        } else {
            memset(&ds, 0, sizeof ds);
            ipc_fill_perm(&s->o, &ds.perm);
            ds.otime = s->otime;
            ds.ctime = s->ctime;
            ds.nsems = (uint64_t)s->nsems;
        }
        break;
    case SETVAL:
        if (!ipc_perm_ok(&s->o, me, IPC_W)) { r = -EACCES; break; }
        if ((int)arg < 0 || (int)arg > SEMVMX) { r = -ERANGE; break; }
        s->sem[num].val = (int)arg;
        s->sem[num].pid = task_tgid(me);
        clear_undo(s, (int)num);
        s->ctime = ipc_now();
        waitq_wake_all(&s->wq);
        break;
    case SETALL:
        if (!ipc_perm_ok(&s->o, me, IPC_W)) { r = -EACCES; break; }
        for (int i = 0; i < s->nsems; i++) if (vals[i] > SEMVMX) r = -ERANGE;
        if (r) break;
        for (int i = 0; i < s->nsems; i++) {
            s->sem[i].val = vals[i];
            s->sem[i].pid = task_tgid(me);
        }
        clear_undo(s, -1);
        s->ctime = ipc_now();
        waitq_wake_all(&s->wq);
        break;
    case IPC_SET_K:
        if (!ipc_owner_ok(&s->o, me)) { r = -EPERM; break; }
        ipc_set_perm(&s->o, &ds.perm);
        s->ctime = ipc_now();
        break;
    case IPC_RMID_K:
        if (!ipc_owner_ok(&s->o, me)) { r = -EPERM; break; }
        waitq_wake_all(&s->wq);
        ipc_remove_locked(&g_ns, &s->o);
        break;
    default:
        r = -EINVAL;
    }
    spinlock_release_irqrestore(&g_ns.lock, f);

    if (r == 0 && cmd == GETALL && syscall_copy_to_user((void *)arg, vals, (size_t)nout * sizeof(uint16_t)) < 0)
        return -EFAULT;
    if (r == 0 && cmd == IPC_STAT_K && syscall_copy_to_user((void *)arg, &ds, sizeof ds) < 0)
        return -EFAULT;
    return r;
}

void sem_exit_group(task_t *leader)
{
    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    sem_undo_t *u = leader->sem_undo;
    leader->sem_undo = NULL;
    while (u) {
        sem_undo_t *next = u->pnext;
        semset_t *s = u->set;
        if (s) {
            for (int i = 0; i < u->nsems && i < s->nsems; i++) {
                int v = s->sem[i].val + u->adj[i];
                if (v < 0) v = 0;
                if (v > SEMVMX) v = SEMVMX;
                s->sem[i].val = v;
                if (u->adj[i]) s->sem[i].pid = leader->pid;
            }
            sem_undo_t **pp = &s->undos;
            while (*pp && *pp != u) pp = &(*pp)->snext;
            if (*pp) *pp = u->snext;
            waitq_wake_all(&s->wq);
        }
        kfree(u);
        u = next;
    }
    spinlock_release_irqrestore(&g_ns.lock, f);
}
