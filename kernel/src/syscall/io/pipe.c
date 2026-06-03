#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/fs/vfs.h"
#include <string.h>
#include <stdlib.h>

#define PIPE_BUFSZ 4096

typedef struct {
    char     buf[PIPE_BUFSZ];
    uint32_t head, tail;
    int      readers, writers;
    spinlock_t lock;
    task_t  *read_waiter;
    task_t  *write_waiter;
} pipe_shared_t;

typedef struct {
    pipe_shared_t *shared;
    int            end;
} pipe_vdata_t;

static void pipe_wake_other(task_t **slot) {
    task_t *w = *slot;
    if (!w) return;
    *slot = NULL;
    task_unblock(w);
}

static int64_t pipe_read_op(vnode_t *n, void *buf, size_t len, uint64_t off)
{
    (void)off;
    pipe_vdata_t  *vd = (pipe_vdata_t *)n->fs_data;
    pipe_shared_t *ps = vd->shared;
    size_t got = 0;
    char *dst = (char *)buf;
    task_t *me = syscall_cur_task();

    while (got < len) {
        uint64_t f = spinlock_acquire_irqsave(&ps->lock);
        if (ps->head == ps->tail) {
            int writers = ps->writers;
            if (writers == 0 || got > 0) {
                spinlock_release_irqrestore(&ps->lock, f);
                break;
            }
            if (me && me->pending_kill) {
                spinlock_release_irqrestore(&ps->lock, f);
                return -EINTR;
            }
            if (me) {
                ps->read_waiter = me;
                me->runnable = false;
                me->state    = TASK_BLOCKED;
            }
            spinlock_release_irqrestore(&ps->lock, f);
            if (me) sched_reschedule();
            else    task_yield();
            continue;
        }
        while (got < len && ps->head != ps->tail) {
            dst[got++] = ps->buf[ps->head];
            ps->head = (ps->head + 1) % PIPE_BUFSZ;
        }
        pipe_wake_other(&ps->write_waiter);
        spinlock_release_irqrestore(&ps->lock, f);
    }
    return (int64_t)got;
}

static int64_t pipe_write_op(vnode_t *n, const void *buf, size_t len, uint64_t off)
{
    (void)off;
    pipe_vdata_t  *vd = (pipe_vdata_t *)n->fs_data;
    pipe_shared_t *ps = vd->shared;
    const char *src = (const char *)buf;
    size_t done = 0;
    task_t *me = syscall_cur_task();

    while (done < len) {
        uint64_t f = spinlock_acquire_irqsave(&ps->lock);
        if (ps->readers == 0) {
            spinlock_release_irqrestore(&ps->lock, f);
            return (done > 0) ? (int64_t)done : -EPIPE;
        }
        uint32_t next = (ps->tail + 1) % PIPE_BUFSZ;
        if (next == ps->head) {
            if (me && me->pending_kill) {
                spinlock_release_irqrestore(&ps->lock, f);
                return (done > 0) ? (int64_t)done : -EINTR;
            }
            if (me) {
                ps->write_waiter = me;
                me->runnable = false;
                me->state    = TASK_BLOCKED;
            }
            spinlock_release_irqrestore(&ps->lock, f);
            if (me) sched_reschedule();
            else    task_yield();
            continue;
        }
        while (done < len) {
            uint32_t n2 = (ps->tail + 1) % PIPE_BUFSZ;
            if (n2 == ps->head) break;
            ps->buf[ps->tail] = src[done++];
            ps->tail = n2;
        }
        pipe_wake_other(&ps->read_waiter);
        spinlock_release_irqrestore(&ps->lock, f);
    }
    return (int64_t)done;
}

static int pipe_stat_op(vnode_t *n, vfs_stat_t *out)
{
    memset(out, 0, sizeof(*out));
    out->st_ino  = n->ino;
    out->st_type = VFS_NODE_PIPE;
    return 0;
}

static void pipe_ref_op(vnode_t *n) { (void)n; }

static void pipe_unref_op(vnode_t *n)
{
    pipe_vdata_t  *vd = (pipe_vdata_t *)n->fs_data;
    pipe_shared_t *ps = vd->shared;

    uint64_t f = spinlock_acquire_irqsave(&ps->lock);
    if (vd->end == 0) ps->readers--;
    else              ps->writers--;
    int r = ps->readers;
    int w = ps->writers;
    if (r == 0) pipe_wake_other(&ps->write_waiter);
    if (w == 0) pipe_wake_other(&ps->read_waiter);
    spinlock_release_irqrestore(&ps->lock, f);

    free(vd); free(n);

    if (r <= 0 && w <= 0)
        free(ps);
}

static const vnode_ops_t pipe_read_ops = {
    .read   = pipe_read_op,
    .stat   = pipe_stat_op,
    .ref    = pipe_ref_op,
    .unref  = pipe_unref_op,
};
static const vnode_ops_t pipe_write_ops = {
    .write  = pipe_write_op,
    .stat   = pipe_stat_op,
    .ref    = pipe_ref_op,
    .unref  = pipe_unref_op,
};

int64_t sys_pipe(uint64_t fds_ptr)
{
    if (!syscall_uptr_validate((void *)fds_ptr, 2 * sizeof(int))) return -EFAULT;

    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -ENOMEM;

    pipe_shared_t *ps = (pipe_shared_t *)malloc(sizeof(pipe_shared_t));
    if (!ps) return -ENOMEM;
    memset(ps, 0, sizeof(*ps));
    ps->readers = 1; ps->writers = 1;

    vnode_t     *rv = (vnode_t *)malloc(sizeof(vnode_t));
    vnode_t     *wv = (vnode_t *)malloc(sizeof(vnode_t));
    pipe_vdata_t *rd = (pipe_vdata_t *)malloc(sizeof(pipe_vdata_t));
    pipe_vdata_t *wd = (pipe_vdata_t *)malloc(sizeof(pipe_vdata_t));
    if (!rv || !wv || !rd || !wd) {
        free(ps); free(rv); free(wv); free(rd); free(wd);
        return -ENOMEM;
    }
    memset(rv, 0, sizeof(*rv)); memset(wv, 0, sizeof(*wv));
    rd->shared = ps; rd->end = 0;
    wd->shared = ps; wd->end = 1;

    static uint64_t pipe_ino = 0x10000;
    rv->type = VFS_NODE_PIPE; rv->mode = 0600; rv->ino = pipe_ino++;
    rv->ops  = &pipe_read_ops; rv->fs_data = rd; rv->refcount = 1;
    wv->type = VFS_NODE_PIPE; wv->mode = 0600; wv->ino = pipe_ino++;
    wv->ops  = &pipe_write_ops; wv->fs_data = wd; wv->refcount = 1;

    vfs_file_t *rf = vfs_file_alloc();
    vfs_file_t *wf = vfs_file_alloc();
    if (!rf || !wf) {
        free(ps); free(rv); free(wv); free(rd); free(wd);
        if (rf) vfs_file_free(rf);
        if (wf) vfs_file_free(wf);
        return -ENOMEM;
    }
    rf->vnode = rv; rf->flags = O_RDONLY; rf->offset = 0; rf->refcount = 1;
    wf->vnode = wv; wf->flags = O_WRONLY; wf->offset = 0; wf->refcount = 1;

    int rfd = fd_alloc(t->fd_table, rf, 0);
    int wfd = fd_alloc(t->fd_table, wf, 0);
    if (rfd < 0 || wfd < 0) {
        fd_close(t->fd_table, rfd); fd_close(t->fd_table, wfd); return -EMFILE;
    }

    int fds[2] = {rfd, wfd};
    memcpy((void *)fds_ptr, fds, sizeof(fds));
    return 0;
}
