#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include <string.h>
#include <stdlib.h>

#include "../../../include/signal/signal.h"
#include "../../../include/drivers/timer.h"

#define PIPE_BUFSZ   65536u
#define PIPE_ATOMIC  4096u
#define PIPE_WAITERS 16

typedef struct pipe_shared {
    char    *buf;
    uint32_t head, used;
    int      readers, writers;
    spinlock_t lock;
    task_t  *rwait[PIPE_WAITERS];
    task_t  *wwait[PIPE_WAITERS];
    task_t  *owait[PIPE_WAITERS];
    uint64_t rgen, wgen;
    int      is_fifo;
    uintptr_t key_fs;
    uint64_t key_id;
    struct pipe_shared *next;
} pipe_shared_t;

static spinlock_t     g_fifo_lock = SPINLOCK_INIT;
static pipe_shared_t *g_fifos;

typedef struct {
    pipe_shared_t *shared;
    int            end;
} pipe_vdata_t;

static void wake_all(task_t **list) {
    for (int i = 0; i < PIPE_WAITERS; i++) {
        task_t *w = list[i];
        if (!w) continue;
        list[i] = NULL;
        task_unblock(w);
    }
}

static int add_waiter(task_t **list, task_t *me) {
    for (int i = 0; i < PIPE_WAITERS; i++)
        if (list[i] == me) return 1;
    for (int i = 0; i < PIPE_WAITERS; i++)
        if (!list[i]) { list[i] = me; return 1; }
    return 0;
}

static void drop_waiter(task_t **list, task_t *me) {
    for (int i = 0; i < PIPE_WAITERS; i++)
        if (list[i] == me) list[i] = NULL;
}

static bool interrupted(task_t *me) {
    return me && (me->pending_kill || signal_pending_deliverable(me));
}

static void block_on(pipe_shared_t *ps, task_t **list, task_t *me, uint64_t f) {
    if (!me) {
        spinlock_release_irqrestore(&ps->lock, f);
        task_yield();
        return;
    }
    int listed = add_waiter(list, me);
    me->wakeup_time_ns = listed ? 0 : sched_now_ns() + 1000000ULL;
    me->runnable = false;
    me->state    = TASK_BLOCKED;
    if (!listed) sched_note_wakeup(me->wakeup_time_ns);
    spinlock_release_irqrestore(&ps->lock, f);
    sched_reschedule();
    me->wakeup_time_ns = 0;
    f = spinlock_acquire_irqsave(&ps->lock);
    drop_waiter(list, me);
    spinlock_release_irqrestore(&ps->lock, f);
}

static int64_t pipe_read_op(vnode_t *n, void *buf, size_t len, uint64_t off)
{
    (void)off;
    pipe_vdata_t  *vd = (pipe_vdata_t *)n->fs_data;
    pipe_shared_t *ps = vd->shared;
    char *dst = (char *)buf;
    task_t *me = syscall_cur_task();
    if (len == 0) return 0;

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&ps->lock);
        if (ps->used > 0) {
            size_t got = 0;
            while (got < len && ps->used > 0) {
                uint32_t chunk = PIPE_BUFSZ - ps->head;
                if (chunk > ps->used) chunk = ps->used;
                if (chunk > len - got) chunk = (uint32_t)(len - got);
                memcpy(dst + got, ps->buf + ps->head, chunk);
                got += chunk;
                ps->head = (ps->head + chunk) % PIPE_BUFSZ;
                ps->used -= chunk;
            }
            wake_all(ps->wwait);
            spinlock_release_irqrestore(&ps->lock, f);
            return (int64_t)got;
        }
        if (ps->writers == 0) {
            spinlock_release_irqrestore(&ps->lock, f);
            return 0;
        }
        if (vfs_io_nonblock()) {
            spinlock_release_irqrestore(&ps->lock, f);
            return -EAGAIN;
        }
        if (interrupted(me)) {
            spinlock_release_irqrestore(&ps->lock, f);
            return -EINTR;
        }
        block_on(ps, ps->rwait, me, f);
    }
}

static int64_t pipe_write_op(vnode_t *n, const void *buf, size_t len, uint64_t off)
{
    (void)off;
    pipe_vdata_t  *vd = (pipe_vdata_t *)n->fs_data;
    pipe_shared_t *ps = vd->shared;
    const char *src = (const char *)buf;
    size_t done = 0;
    task_t *me = syscall_cur_task();
    bool atomic = len <= PIPE_ATOMIC;

    while (done < len) {
        uint64_t f = spinlock_acquire_irqsave(&ps->lock);
        if (ps->readers == 0) {
            spinlock_release_irqrestore(&ps->lock, f);
            if (me) signal_send(me, SIGPIPE);
            return done > 0 ? (int64_t)done : -EPIPE;
        }
        uint32_t space = PIPE_BUFSZ - ps->used;
        size_t need = atomic ? len - done : 1;
        if (space >= need) {
            while (done < len && ps->used < PIPE_BUFSZ) {
                uint32_t tail = (ps->head + ps->used) % PIPE_BUFSZ;
                uint32_t chunk = PIPE_BUFSZ - tail;
                if (chunk > PIPE_BUFSZ - ps->used) chunk = PIPE_BUFSZ - ps->used;
                if (chunk > len - done) chunk = (uint32_t)(len - done);
                memcpy(ps->buf + tail, src + done, chunk);
                done += chunk;
                ps->used += chunk;
            }
            wake_all(ps->rwait);
            spinlock_release_irqrestore(&ps->lock, f);
            continue;
        }
        if (vfs_io_nonblock()) {
            spinlock_release_irqrestore(&ps->lock, f);
            return done > 0 ? (int64_t)done : -EAGAIN;
        }
        if (interrupted(me)) {
            spinlock_release_irqrestore(&ps->lock, f);
            return done > 0 ? (int64_t)done : -EINTR;
        }
        block_on(ps, ps->wwait, me, f);
    }
    return (int64_t)done;
}

static int pipe_stat_op(vnode_t *n, vfs_stat_t *out)
{
    memset(out, 0, sizeof(*out));
    out->st_ino  = n->ino;
    out->st_type = VFS_NODE_PIPE;
    out->st_mode = n->mode;
    out->st_uid  = n->uid;
    out->st_gid  = n->gid;
    return 0;
}

static void pipe_ref_op(vnode_t *n) { (void)n; }

static void pipe_unref_op(vnode_t *n)
{
    pipe_vdata_t  *vd = (pipe_vdata_t *)n->fs_data;
    pipe_shared_t *ps = vd->shared;

    uint64_t gf = 0;
    if (ps->is_fifo) gf = spinlock_acquire_irqsave(&g_fifo_lock);
    uint64_t f = spinlock_acquire_irqsave(&ps->lock);
    if (vd->end != 1) ps->readers--;
    if (vd->end != 0) ps->writers--;
    int r = ps->readers;
    int w = ps->writers;
    if (r == 0) wake_all(ps->wwait);
    if (w == 0) wake_all(ps->rwait);
    spinlock_release_irqrestore(&ps->lock, f);
    bool last = r <= 0 && w <= 0;
    if (ps->is_fifo) {
        if (last) {
            pipe_shared_t **pp = &g_fifos;
            while (*pp && *pp != ps) pp = &(*pp)->next;
            if (*pp) *pp = ps->next;
        }
        spinlock_release_irqrestore(&g_fifo_lock, gf);
    }

    free(vd); free(n);

    if (last) {
        free(ps->buf);
        free(ps);
    }
}

static int pipe_poll_op(vnode_t *n, int events) {
    (void)events;
    pipe_vdata_t  *vd = (pipe_vdata_t *)n->fs_data;
    pipe_shared_t *ps = vd->shared;
    int r = 0;
    uint64_t f = spinlock_acquire_irqsave(&ps->lock);
    if (vd->end != 1) {
        if (ps->used > 0) r |= POLLIN;
        if (ps->writers == 0 && (!ps->is_fifo || ps->wgen > 0)) r |= POLLHUP;
    }
    if (vd->end != 0) {
        if (PIPE_BUFSZ - ps->used >= PIPE_ATOMIC) r |= POLLOUT;
        if (ps->readers == 0) r |= POLLERR;
    }
    spinlock_release_irqrestore(&ps->lock, f);
    return r;
}

static const vnode_ops_t pipe_read_ops = {
    .read   = pipe_read_op,
    .stat   = pipe_stat_op,
    .ref    = pipe_ref_op,
    .unref  = pipe_unref_op,
    .poll   = pipe_poll_op,
};
static const vnode_ops_t pipe_write_ops = {
    .write  = pipe_write_op,
    .stat   = pipe_stat_op,
    .ref    = pipe_ref_op,
    .unref  = pipe_unref_op,
    .poll   = pipe_poll_op,
};
static const vnode_ops_t pipe_rdwr_ops = {
    .read   = pipe_read_op,
    .write  = pipe_write_op,
    .stat   = pipe_stat_op,
    .ref    = pipe_ref_op,
    .unref  = pipe_unref_op,
    .poll   = pipe_poll_op,
};

static pipe_shared_t *fifo_get(uintptr_t fs, uint64_t id)
{
    for (pipe_shared_t *p = g_fifos; p; p = p->next)
        if (p->key_fs == fs && p->key_id == id) return p;
    pipe_shared_t *ps = (pipe_shared_t *)malloc(sizeof(pipe_shared_t));
    if (!ps) return NULL;
    memset(ps, 0, sizeof(*ps));
    ps->buf = (char *)malloc(PIPE_BUFSZ);
    if (!ps->buf) { free(ps); return NULL; }
    ps->is_fifo = 1;
    ps->key_fs = fs;
    ps->key_id = id;
    ps->next = g_fifos;
    g_fifos = ps;
    return ps;
}

static void fifo_release_unused(pipe_shared_t *ps)
{
    if (ps->readers > 0 || ps->writers > 0) return;
    pipe_shared_t **pp = &g_fifos;
    while (*pp && *pp != ps) pp = &(*pp)->next;
    if (*pp) *pp = ps->next;
    free(ps->buf);
    free(ps);
}

int fifo_open(vnode_t *node, int flags, vnode_t **out)
{
    uintptr_t kfs = (uintptr_t)node;
    uint64_t kid = 0;
    if (node->ops && node->ops->cache_id) {
        uintptr_t fs = 0;
        uint64_t id = 0;
        if (node->ops->cache_id(node, &fs, &id) == 0) { kfs = fs; kid = id; }
    }
    int acc = flags & O_ACCMODE;
    bool nonblock = (flags & O_NONBLOCK) != 0;
    task_t *me = syscall_cur_task();

    vnode_t *v = (vnode_t *)malloc(sizeof(vnode_t));
    pipe_vdata_t *vd = (pipe_vdata_t *)malloc(sizeof(pipe_vdata_t));
    if (!v || !vd) { free(v); free(vd); return -ENOMEM; }

    uint64_t gf = spinlock_acquire_irqsave(&g_fifo_lock);
    pipe_shared_t *ps = fifo_get(kfs, kid);
    if (!ps) {
        spinlock_release_irqrestore(&g_fifo_lock, gf);
        free(v); free(vd);
        return -ENOMEM;
    }
    uint64_t f = spinlock_acquire_irqsave(&ps->lock);
    if (acc == O_WRONLY && nonblock && ps->readers == 0) {
        spinlock_release_irqrestore(&ps->lock, f);
        fifo_release_unused(ps);
        spinlock_release_irqrestore(&g_fifo_lock, gf);
        free(v); free(vd);
        return -ENXIO;
    }
    int end = acc == O_RDONLY ? 0 : acc == O_WRONLY ? 1 : 2;
    if (end != 1) { ps->readers++; ps->rgen++; }
    if (end != 0) { ps->writers++; ps->wgen++; }
    wake_all(ps->owait);
    wake_all(ps->rwait);
    wake_all(ps->wwait);
    spinlock_release_irqrestore(&g_fifo_lock, gf);

    if (end != 2 && !nonblock) {
        uint64_t want = end == 0 ? ps->wgen : ps->rgen;
        for (;;) {
            int peers = end == 0 ? ps->writers : ps->readers;
            uint64_t gen = end == 0 ? ps->wgen : ps->rgen;
            if (peers > 0 || gen != want) break;
            if (interrupted(me)) {
                if (end == 0) ps->readers--; else ps->writers--;
                spinlock_release_irqrestore(&ps->lock, f);
                gf = spinlock_acquire_irqsave(&g_fifo_lock);
                f = spinlock_acquire_irqsave(&ps->lock);
                bool unused = ps->readers <= 0 && ps->writers <= 0;
                spinlock_release_irqrestore(&ps->lock, f);
                if (unused) fifo_release_unused(ps);
                spinlock_release_irqrestore(&g_fifo_lock, gf);
                free(v); free(vd);
                return -EINTR;
            }
            block_on(ps, ps->owait, me, f);
            f = spinlock_acquire_irqsave(&ps->lock);
        }
    }
    spinlock_release_irqrestore(&ps->lock, f);

    memset(v, 0, sizeof(*v));
    vd->shared = ps;
    vd->end = end;
    v->type = VFS_NODE_PIPE;
    v->mode = node->mode;
    v->uid = node->uid;
    v->gid = node->gid;
    v->ino = node->ino;
    v->rdev = VFS_PIPE_ENDPOINT;
    v->ops = end == 0 ? &pipe_read_ops : end == 1 ? &pipe_write_ops : &pipe_rdwr_ops;
    v->fs_data = vd;
    v->refcount = 1;
    *out = v;
    return 0;
}

int64_t sys_pipe(uint64_t fds_ptr)
{
    if (!syscall_uptr_validate((void *)fds_ptr, 2 * sizeof(int))) return -EFAULT;

    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -ENOMEM;

    pipe_shared_t *ps = (pipe_shared_t *)malloc(sizeof(pipe_shared_t));
    if (!ps) return -ENOMEM;
    memset(ps, 0, sizeof(*ps));
    ps->buf = (char *)malloc(PIPE_BUFSZ);
    if (!ps->buf) { free(ps); return -ENOMEM; }
    ps->readers = 1; ps->writers = 1;

    vnode_t     *rv = (vnode_t *)malloc(sizeof(vnode_t));
    vnode_t     *wv = (vnode_t *)malloc(sizeof(vnode_t));
    pipe_vdata_t *rd = (pipe_vdata_t *)malloc(sizeof(pipe_vdata_t));
    pipe_vdata_t *wd = (pipe_vdata_t *)malloc(sizeof(pipe_vdata_t));
    if (!rv || !wv || !rd || !wd) {
        free(ps->buf); free(ps); free(rv); free(wv); free(rd); free(wd);
        return -ENOMEM;
    }
    memset(rv, 0, sizeof(*rv)); memset(wv, 0, sizeof(*wv));
    rd->shared = ps; rd->end = 0;
    wd->shared = ps; wd->end = 1;

    static uint64_t pipe_ino = 0x10000;
    rv->type = VFS_NODE_PIPE; rv->mode = 0600; rv->ino = pipe_ino++; rv->rdev = VFS_PIPE_ENDPOINT;
    rv->ops  = &pipe_read_ops; rv->fs_data = rd; rv->refcount = 1;
    wv->type = VFS_NODE_PIPE; wv->mode = 0600; wv->ino = pipe_ino++; wv->rdev = VFS_PIPE_ENDPOINT;
    wv->ops  = &pipe_write_ops; wv->fs_data = wd; wv->refcount = 1;

    vfs_file_t *rf = vfs_file_alloc();
    vfs_file_t *wf = vfs_file_alloc();
    if (!rf || !wf) {
        free(ps->buf); free(ps); free(rv); free(wv); free(rd); free(wd);
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
