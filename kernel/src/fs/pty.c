#include "../../include/fs/vfs.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/spinlock.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdlib.h>

#define PTY_BUF 8192
#define TERMIOS_SZ 48

#define TCGETS        0x5401
#define TCSETS        0x5402
#define TCSETSW       0x5403
#define TCSETSF       0x5404
#define TIOCGWINSZ    0x5413
#define TIOCSWINSZ    0x5414
#define TIOCGCURSOR   0x5480
#define TIOCSNONBLOCK 0x5481

typedef struct {
    char     buf[PTY_BUF];
    uint32_t head, tail;
    task_t  *reader;
    task_t  *writer;
} pty_ring;

typedef struct {
    pty_ring m2s;
    pty_ring s2m;
    uint8_t  termios[TERMIOS_SZ];
    uint16_t ws[4];
    int      m_open, s_open;
    int      m_nonblock, s_nonblock;
    spinlock_t lock;
} pty_t;

static const vnode_ops_t pty_master_ops;
static const vnode_ops_t pty_slave_ops;

static int ring_count(pty_ring *r) { return (int)((r->tail - r->head + PTY_BUF) % PTY_BUF); }
static int ring_free(pty_ring *r)  { return PTY_BUF - 1 - ring_count(r); }

static int ring_put(pty_ring *r, const char *src, int n) {
    int wr = 0;
    while (wr < n && ring_free(r) > 0) {
        r->buf[r->tail] = src[wr++];
        r->tail = (r->tail + 1) % PTY_BUF;
    }
    return wr;
}
static int ring_get(pty_ring *r, char *dst, int n) {
    int rd = 0;
    while (rd < n && r->head != r->tail) {
        dst[rd++] = r->buf[r->head];
        r->head = (r->head + 1) % PTY_BUF;
    }
    return rd;
}

static int64_t pty_do_read(pty_t *p, pty_ring *in, int *other_open, int nonblock,
                           void *buf, size_t len) {
    task_t *me = syscall_cur_task();
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&p->lock);
        if (ring_count(in) > 0) {
            int n = ring_get(in, buf, (int)len);
            task_t *w = in->writer; in->writer = NULL;
            spinlock_release_irqrestore(&p->lock, f);
            if (w) task_unblock(w);
            return n;
        }
        if (!*other_open) { spinlock_release_irqrestore(&p->lock, f); return 0; }
        if (nonblock) { spinlock_release_irqrestore(&p->lock, f); return -EAGAIN; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&p->lock, f); return -EINTR; }
        if (me) { in->reader = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&p->lock, f);
        if (me) sched_reschedule(); else task_yield();
    }
}

static int64_t pty_do_write(pty_t *p, pty_ring *out, int *other_open, int nonblock,
                            const void *buf, size_t len) {
    task_t *me = syscall_cur_task();
    const char *src = buf;
    size_t done = 0;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&p->lock);
        if (!*other_open) { spinlock_release_irqrestore(&p->lock, f); return done ? (int64_t)done : -EPIPE; }
        if (ring_free(out) > 0) {
            int n = ring_put(out, src + done, (int)(len - done));
            task_t *r = out->reader; out->reader = NULL;
            spinlock_release_irqrestore(&p->lock, f);
            if (r) task_unblock(r);
            done += n;
            if (done >= len) return (int64_t)done;
            continue;
        }
        if (nonblock) { spinlock_release_irqrestore(&p->lock, f); return done ? (int64_t)done : -EAGAIN; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&p->lock, f); return done ? (int64_t)done : -EINTR; }
        if (me) { out->writer = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&p->lock, f);
        if (me) sched_reschedule(); else task_yield();
    }
}

static int64_t pty_master_read(vnode_t *n, void *buf, size_t len, uint64_t off) {
    (void)off; pty_t *p = n->fs_data;
    return pty_do_read(p, &p->s2m, &p->s_open, p->m_nonblock, buf, len);
}
static int64_t pty_master_write(vnode_t *n, const void *buf, size_t len, uint64_t off) {
    (void)off; pty_t *p = n->fs_data;
    return pty_do_write(p, &p->m2s, &p->s_open, p->m_nonblock, buf, len);
}
static int64_t pty_slave_read(vnode_t *n, void *buf, size_t len, uint64_t off) {
    (void)off; pty_t *p = n->fs_data;
    return pty_do_read(p, &p->m2s, &p->m_open, p->s_nonblock, buf, len);
}
static int64_t pty_slave_write(vnode_t *n, const void *buf, size_t len, uint64_t off) {
    (void)off; pty_t *p = n->fs_data;
    return pty_do_write(p, &p->s2m, &p->m_open, p->s_nonblock, buf, len);
}

static int64_t pty_ioctl(vnode_t *n, uint64_t req, void *arg, int is_master) {
    pty_t *p = n->fs_data;
    switch (req) {
        case TCGETS: if (arg) memcpy(arg, p->termios, TERMIOS_SZ); return 0;
        case TCSETS: case TCSETSW: case TCSETSF: if (arg) memcpy(p->termios, arg, TERMIOS_SZ); return 0;
        case TIOCGWINSZ: if (arg) memcpy(arg, p->ws, 8); return 0;
        case TIOCSWINSZ: if (arg) memcpy(p->ws, arg, 8); return 0;
        case TIOCGCURSOR: return -1;
        case TIOCSNONBLOCK: {
            int v = arg ? *(int *)arg : 0;
            if (is_master) p->m_nonblock = v ? 1 : 0; else p->s_nonblock = v ? 1 : 0;
            return 0;
        }
        default: return 0;
    }
}
static int64_t pty_master_ioctl(vnode_t *n, uint64_t req, void *arg) { return pty_ioctl(n, req, arg, 1); }
static int64_t pty_slave_ioctl(vnode_t *n, uint64_t req, void *arg) { return pty_ioctl(n, req, arg, 0); }

static int pty_stat(vnode_t *n, vfs_stat_t *out) {
    (void)n; memset(out, 0, sizeof(*out)); out->st_type = VFS_NODE_CHARDEV; out->st_size = 0; return 0;
}
static void pty_ref(vnode_t *n) { (void)n; }

static void pty_close_end(vnode_t *n, int is_master) {
    pty_t *p = n->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&p->lock);
    if (is_master) p->m_open = 0; else p->s_open = 0;
    task_t *wr = p->s2m.reader, *ww = p->s2m.writer, *mr = p->m2s.reader, *mw = p->m2s.writer;
    p->s2m.reader = p->s2m.writer = p->m2s.reader = p->m2s.writer = NULL;
    int both = (!p->m_open && !p->s_open);
    spinlock_release_irqrestore(&p->lock, f);
    if (wr) task_unblock(wr);
    if (ww) task_unblock(ww);
    if (mr) task_unblock(mr);
    if (mw) task_unblock(mw);
    free(n);
    if (both) free(p);
}
static void pty_master_unref(vnode_t *n) { pty_close_end(n, 1); }
static void pty_slave_unref(vnode_t *n) { pty_close_end(n, 0); }

static const vnode_ops_t pty_master_ops = {
    .read = pty_master_read, .write = pty_master_write, .ioctl = pty_master_ioctl,
    .stat = pty_stat, .ref = pty_ref, .unref = pty_master_unref,
};
static const vnode_ops_t pty_slave_ops = {
    .read = pty_slave_read, .write = pty_slave_write, .ioctl = pty_slave_ioctl,
    .stat = pty_stat, .ref = pty_ref, .unref = pty_slave_unref,
};

int64_t sys_openpty(uint64_t ufds) {
    if (!syscall_uptr_validate((void *)ufds, sizeof(int) * 2)) return -EFAULT;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -ENOMEM;

    pty_t *p = calloc(1, sizeof(*p));
    if (!p) return -ENOMEM;
    p->m_open = 1; p->s_open = 1;
    p->ws[0] = 24; p->ws[1] = 80;
    p->termios[12] = 0x0b;

    vnode_t *mv = calloc(1, sizeof(*mv));
    vnode_t *sv = calloc(1, sizeof(*sv));
    if (!mv || !sv) { free(mv); free(sv); free(p); return -ENOMEM; }
    mv->type = VFS_NODE_CHARDEV; mv->mode = 0600; mv->ops = &pty_master_ops; mv->fs_data = p; mv->refcount = 1;
    sv->type = VFS_NODE_CHARDEV; sv->mode = 0600; sv->ops = &pty_slave_ops;  sv->fs_data = p; sv->refcount = 1;

    vfs_file_t *mf = vfs_file_alloc();
    vfs_file_t *sf = vfs_file_alloc();
    if (!mf || !sf) { if (mf) vfs_file_free(mf); if (sf) vfs_file_free(sf); free(mv); free(sv); free(p); return -ENOMEM; }
    mf->vnode = mv; mf->flags = O_RDWR; mf->offset = 0; mf->refcount = 1;
    sf->vnode = sv; sf->flags = O_RDWR; sf->offset = 0; sf->refcount = 1;

    int mfd = fd_alloc(t->fd_table, mf, 0);
    if (mfd < 0) { vfs_file_free(mf); vfs_file_free(sf); free(mv); free(sv); free(p); return -EMFILE; }
    int sfd = fd_alloc(t->fd_table, sf, 0);
    if (sfd < 0) { vfs_file_free(sf); free(sv); return -EMFILE; }

    int fds[2] = { mfd, sfd };
    if (syscall_copy_to_user((void *)ufds, fds, sizeof fds) < 0) return -EFAULT;
    return 0;
}
