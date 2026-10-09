#include "../../include/ipc/ipc.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/capabilities.h"
#include "../../include/syscall/syscall_internal.h"

extern int64_t clock_realtime_sec(void);

int64_t ipc_now(void)
{
    return clock_realtime_sec();
}

static bool privileged(task_t *t)
{
    return t->uid == 0 || cap_has(t->capabilities, CAP_SYSADMIN);
}

bool ipc_perm_ok(ipc_obj_t *o, task_t *t, int want)
{
    int req = (want | (want >> 3) | (want >> 6)) & 7;
    if (!req || privileged(t)) return true;
    int granted = (int)o->mode;
    if (t->uid == o->uid || t->uid == o->cuid) granted >>= 6;
    else if (t->gid == o->gid || t->gid == o->cgid) granted >>= 3;
    return (req & ~granted & 7) == 0;
}

bool ipc_owner_ok(ipc_obj_t *o, task_t *t)
{
    return privileged(t) || t->uid == o->uid || t->uid == o->cuid;
}

void ipc_fill_perm(ipc_obj_t *o, ipc_perm_user_t *p)
{
    p->key = o->key;
    p->uid = o->uid;
    p->gid = o->gid;
    p->cuid = o->cuid;
    p->cgid = o->cgid;
    p->mode = (uint16_t)(o->mode & 0777);
    p->pad1 = 0;
    p->seq = o->seq;
    p->pad2 = 0;
    p->unused1 = p->unused2 = 0;
}

void ipc_set_perm(ipc_obj_t *o, const ipc_perm_user_t *p)
{
    o->uid = p->uid;
    o->gid = p->gid;
    o->mode = (o->mode & ~0777u) | (p->mode & 0777u);
}

ipc_obj_t *ipc_lookup_locked(ipc_ns_t *ns, int id)
{
    if (id < 0) return NULL;
    ipc_obj_t *o = ns->slot[id % IPC_SLOTS];
    return o && o->id == id && !o->dead ? o : NULL;
}

void ipc_put_locked(ipc_ns_t *ns, ipc_obj_t *o)
{
    if (--o->refs == 0 && o->dead) ns->destroy(o);
}

void ipc_remove_locked(ipc_ns_t *ns, ipc_obj_t *o)
{
    int s = o->id % IPC_SLOTS;
    if (ns->slot[s] == o) ns->slot[s] = NULL;
    o->dead = 1;
    if (o->refs == 0) ns->destroy(o);
}

static ipc_obj_t *find_key_locked(ipc_ns_t *ns, int32_t key)
{
    for (int i = 0; i < IPC_SLOTS; i++) {
        ipc_obj_t *o = ns->slot[i];
        if (o && !o->dead && o->key == key) return o;
    }
    return NULL;
}

static int existing(ipc_obj_t *o, task_t *me, int flags, ipc_check_fn check, void *arg)
{
    if ((flags & IPC_CREAT_K) && (flags & IPC_EXCL_K)) return -EEXIST;
    if (!ipc_perm_ok(o, me, flags & 0777)) return -EACCES;
    if (check) {
        int r = check(o, arg);
        if (r < 0) return r;
    }
    return o->id;
}

int ipc_get(ipc_ns_t *ns, int32_t key, int flags, ipc_create_fn create, ipc_check_fn check, void *arg)
{
    task_t *me = syscall_cur_task();
    if (!me) return -ESRCH;

    if (key != IPC_PRIVATE_K) {
        uint64_t f = spinlock_acquire_irqsave(&ns->lock);
        ipc_obj_t *o = find_key_locked(ns, key);
        int r = o ? existing(o, me, flags, check, arg) : 0;
        spinlock_release_irqrestore(&ns->lock, f);
        if (o) return r;
        if (!(flags & IPC_CREAT_K)) return -ENOENT;
    }

    int err = 0;
    ipc_obj_t *n = create(arg, &err);
    if (!n) return err ? err : -ENOMEM;
    n->key = key;
    n->uid = n->cuid = me->uid;
    n->gid = n->cgid = me->gid;
    n->mode = (uint32_t)(flags & 0777);
    n->dead = 0;
    n->refs = 0;

    uint64_t f = spinlock_acquire_irqsave(&ns->lock);
    if (key != IPC_PRIVATE_K) {
        ipc_obj_t *o = find_key_locked(ns, key);
        if (o) {
            int r = existing(o, me, flags, check, arg);
            spinlock_release_irqrestore(&ns->lock, f);
            ns->destroy(n);
            return r;
        }
    }
    int s = -1;
    for (int i = 0; i < IPC_SLOTS; i++)
        if (!ns->slot[i]) { s = i; break; }
    if (s < 0) {
        spinlock_release_irqrestore(&ns->lock, f);
        ns->destroy(n);
        return -ENOSPC;
    }
    ns->seq = (uint16_t)((ns->seq + 1) & 0x7fff);
    n->seq = ns->seq;
    n->id = (int32_t)n->seq * IPC_SLOTS + s;
    ns->slot[s] = n;
    int id = n->id;
    spinlock_release_irqrestore(&ns->lock, f);
    return id;
}
