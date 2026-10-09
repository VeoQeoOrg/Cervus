#include "../../include/ipc/ipc.h"
#include "../../include/memory/pmm.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/waitq.h"
#include "../../include/signal/signal.h"
#include "../../include/syscall/syscall_internal.h"
#include <string.h>

#define MSGMAX       8192
#define MSGMNB       16384
#define MSG_NOERROR  010000
#define MSG_EXCEPT   020000

typedef struct kmsg {
    struct kmsg *next;
    int64_t      type;
    size_t       len;
    char         data[];
} kmsg_t;

typedef struct {
    ipc_obj_t o;
    kmsg_t   *head;
    kmsg_t   *tail;
    uint64_t  cbytes, qnum, qbytes;
    int64_t   stime, rtime, ctime;
    uint32_t  lspid, lrpid;
    waitq_t   rq, wq;
} msq_t;

typedef struct {
    ipc_perm_user_t perm;
    int64_t  stime, rtime, ctime;
    uint64_t cbytes, qnum, qbytes;
    int32_t  lspid, lrpid;
    uint64_t unused[2];
} msqid_ds_user_t;

static void msq_destroy(ipc_obj_t *o)
{
    msq_t *q = (msq_t *)o;
    for (kmsg_t *m = q->head; m;) {
        kmsg_t *n = m->next;
        kfree(m);
        m = n;
    }
    kfree(q);
}

static ipc_ns_t g_ns = { .lock = SPINLOCK_INIT, .destroy = msq_destroy };

static ipc_obj_t *msq_create(void *arg, int *err)
{
    (void)arg; (void)err;
    msq_t *q = kzalloc(sizeof *q);
    if (!q) return NULL;
    q->qbytes = MSGMNB;
    q->ctime = ipc_now();
    return &q->o;
}

int64_t sys_msgget(uint64_t key, uint64_t flags)
{
    return ipc_get(&g_ns, (int32_t)key, (int)flags, msq_create, NULL, NULL);
}

static bool interrupted(task_t *me)
{
    return me->pending_kill || signal_pending_deliverable(me);
}

int64_t sys_msgsnd(uint64_t id, uint64_t uptr, uint64_t size, uint64_t flags)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    if (size > MSGMAX) return -EINVAL;
    kmsg_t *m = kmalloc(sizeof *m + size);
    if (!m) return -ENOMEM;
    if (syscall_copy_from_user(&m->type, (const void *)uptr, sizeof m->type) < 0 ||
        (size && syscall_copy_from_user(m->data, (const void *)(uptr + sizeof(int64_t)), size) < 0)) {
        kfree(m);
        return -EFAULT;
    }
    if (m->type <= 0) { kfree(m); return -EINVAL; }
    m->len = size;
    m->next = NULL;

    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    for (;;) {
        msq_t *q = (msq_t *)ipc_lookup_locked(&g_ns, (int)id);
        if (!q) { spinlock_release_irqrestore(&g_ns.lock, f); kfree(m); return -EINVAL; }
        if (!ipc_perm_ok(&q->o, me, IPC_W)) { spinlock_release_irqrestore(&g_ns.lock, f); kfree(m); return -EACCES; }
        if (q->cbytes + size <= q->qbytes && q->qnum < q->qbytes) {
            if (q->tail) q->tail->next = m; else q->head = m;
            q->tail = m;
            q->cbytes += size;
            q->qnum++;
            q->stime = ipc_now();
            q->lspid = task_tgid(me);
            waitq_wake_all(&q->rq);
            spinlock_release_irqrestore(&g_ns.lock, f);
            return 0;
        }
        if (flags & IPC_NOWAIT_K) { spinlock_release_irqrestore(&g_ns.lock, f); kfree(m); return -EAGAIN; }
        if (interrupted(me)) { spinlock_release_irqrestore(&g_ns.lock, f); kfree(m); return -EINTR; }
        q->o.refs++;
        waitq_block(&q->wq, &g_ns.lock, f, 0);
        f = spinlock_acquire_irqsave(&g_ns.lock);
        bool gone = q->o.dead;
        ipc_put_locked(&g_ns, &q->o);
        if (gone) { spinlock_release_irqrestore(&g_ns.lock, f); kfree(m); return -EIDRM; }
    }
}

static kmsg_t **pick(msq_t *q, int64_t type, int flags)
{
    kmsg_t **best = NULL;
    for (kmsg_t **pp = &q->head; *pp; pp = &(*pp)->next) {
        kmsg_t *m = *pp;
        if (type == 0) return pp;
        if (type > 0) {
            bool match = (flags & MSG_EXCEPT) ? m->type != type : m->type == type;
            if (match) return pp;
        } else if (m->type <= -type && (!best || m->type < (*best)->type)) {
            best = pp;
        }
    }
    return best;
}

int64_t sys_msgrcv(uint64_t id, uint64_t uptr, uint64_t size, uint64_t type, uint64_t flags)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    if ((int64_t)size < 0) return -EINVAL;

    kmsg_t *m = NULL;
    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    for (;;) {
        msq_t *q = (msq_t *)ipc_lookup_locked(&g_ns, (int)id);
        if (!q) { spinlock_release_irqrestore(&g_ns.lock, f); return -EINVAL; }
        if (!ipc_perm_ok(&q->o, me, IPC_R)) { spinlock_release_irqrestore(&g_ns.lock, f); return -EACCES; }
        kmsg_t **pp = pick(q, (int64_t)type, (int)flags);
        if (pp) {
            m = *pp;
            if (m->len > size && !(flags & MSG_NOERROR)) {
                spinlock_release_irqrestore(&g_ns.lock, f);
                return -E2BIG;
            }
            *pp = m->next;
            if (q->tail == m) {
                kmsg_t *t = q->head;
                while (t && t->next) t = t->next;
                q->tail = t;
            }
            q->cbytes -= m->len;
            q->qnum--;
            q->rtime = ipc_now();
            q->lrpid = task_tgid(me);
            waitq_wake_all(&q->wq);
            break;
        }
        if (flags & IPC_NOWAIT_K) { spinlock_release_irqrestore(&g_ns.lock, f); return -ENOMSG; }
        if (interrupted(me)) { spinlock_release_irqrestore(&g_ns.lock, f); return -EINTR; }
        q->o.refs++;
        waitq_block(&q->rq, &g_ns.lock, f, 0);
        f = spinlock_acquire_irqsave(&g_ns.lock);
        bool gone = q->o.dead;
        ipc_put_locked(&g_ns, &q->o);
        if (gone) { spinlock_release_irqrestore(&g_ns.lock, f); return -EIDRM; }
    }
    spinlock_release_irqrestore(&g_ns.lock, f);

    size_t n = m->len < size ? m->len : size;
    int64_t r = (int64_t)n;
    if (syscall_copy_to_user((void *)uptr, &m->type, sizeof m->type) < 0 ||
        (n && syscall_copy_to_user((void *)(uptr + sizeof(int64_t)), m->data, n) < 0))
        r = -EFAULT;
    kfree(m);
    return r;
}

int64_t sys_msgctl(uint64_t id, uint64_t cmd, uint64_t buf)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;
    cmd &= ~0x100ULL;
    msqid_ds_user_t ds;
    if (cmd == IPC_SET_K && syscall_copy_from_user(&ds, (const void *)buf, sizeof ds) < 0) return -EFAULT;

    uint64_t f = spinlock_acquire_irqsave(&g_ns.lock);
    msq_t *q = (msq_t *)ipc_lookup_locked(&g_ns, (int)id);
    if (!q) { spinlock_release_irqrestore(&g_ns.lock, f); return -EINVAL; }
    int64_t r = 0;
    switch (cmd) {
    case IPC_STAT_K:
        if (!ipc_perm_ok(&q->o, me, IPC_R)) { r = -EACCES; break; }
        memset(&ds, 0, sizeof ds);
        ipc_fill_perm(&q->o, &ds.perm);
        ds.stime = q->stime; ds.rtime = q->rtime; ds.ctime = q->ctime;
        ds.cbytes = q->cbytes; ds.qnum = q->qnum; ds.qbytes = q->qbytes;
        ds.lspid = (int32_t)q->lspid; ds.lrpid = (int32_t)q->lrpid;
        break;
    case IPC_SET_K:
        if (!ipc_owner_ok(&q->o, me)) { r = -EPERM; break; }
        if (ds.qbytes > MSGMNB && me->uid != 0) { r = -EPERM; break; }
        ipc_set_perm(&q->o, &ds.perm);
        q->qbytes = ds.qbytes;
        q->ctime = ipc_now();
        waitq_wake_all(&q->wq);
        break;
    case IPC_RMID_K:
        if (!ipc_owner_ok(&q->o, me)) { r = -EPERM; break; }
        waitq_wake_all(&q->rq);
        waitq_wake_all(&q->wq);
        ipc_remove_locked(&g_ns, &q->o);
        break;
    default:
        r = -EINVAL;
    }
    spinlock_release_irqrestore(&g_ns.lock, f);
    if (r == 0 && cmd == IPC_STAT_K && syscall_copy_to_user((void *)buf, &ds, sizeof ds) < 0) return -EFAULT;
    return r;
}
