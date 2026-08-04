#include "../../include/net/socket.h"
#include "../../include/fs/vfs.h"
#include "../../include/fs/poll.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/spinlock.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdlib.h>

#define UNIX_PATH_MAX 108
#define UNIX_BUFSZ    16384
#define UNIX_ACCEPTQ  16

typedef struct unix_sock {
    int      type;
    int      state;
    char     path[UNIX_PATH_MAX];

    uint8_t  rbuf[UNIX_BUFSZ];
    uint32_t rhead, rtail;

    struct unix_sock *peer;
    int      peer_gone;

    vnode_t *accept_q[UNIX_ACCEPTQ];
    int      aqh, aqt, aqc;

    vfs_file_t *fd_inbox[8];
    int      fih, fit, fic;

    task_t  *reader;
    task_t  *acceptor;
    task_t  *fd_waiter;

    struct unix_sock *next;
} unix_sock_t;

enum { U_UNBOUND, U_BOUND, U_LISTEN, U_CONNECTED };

static const vnode_ops_t unix_vnode_ops;
static unix_sock_t       *g_listeners;
static spinlock_t         g_ulock = SPINLOCK_INIT;

int unix_is_vnode(const vnode_t *vn) { return vn && vn->ops == &unix_vnode_ops; }

static uint32_t rb_count(unix_sock_t *s) { return (s->rtail - s->rhead + UNIX_BUFSZ) % UNIX_BUFSZ; }
static uint32_t rb_space(unix_sock_t *s) { return UNIX_BUFSZ - 1 - rb_count(s); }

static vnode_t *unix_make(int type, unix_sock_t **out) {
    unix_sock_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->type = type;
    s->state = U_UNBOUND;
    vnode_t *vn = calloc(1, sizeof(*vn));
    if (!vn) { free(s); return NULL; }
    vn->type = VFS_NODE_CHARDEV;
    vn->mode = 0600;
    vn->ops = &unix_vnode_ops;
    vn->fs_data = s;
    vn->refcount = 1;
    if (out) *out = s;
    return vn;
}

vnode_t *unix_new_vnode(int type) {
    if (type != SOCK_STREAM) return NULL;
    return unix_make(type, NULL);
}

int64_t unix_op_bind(vnode_t *vn, const char *path) {
    unix_sock_t *s = vn->fs_data;
    if (!path[0]) return -EINVAL;
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    if (s->state != U_UNBOUND) { spinlock_release_irqrestore(&g_ulock, f); return -EINVAL; }
    strncpy(s->path, path, UNIX_PATH_MAX - 1);
    s->state = U_BOUND;
    spinlock_release_irqrestore(&g_ulock, f);
    return 0;
}

int64_t unix_op_listen(vnode_t *vn) {
    unix_sock_t *s = vn->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    if (s->state != U_BOUND) { spinlock_release_irqrestore(&g_ulock, f); return -EINVAL; }
    for (unix_sock_t *l = g_listeners; l; l = l->next)
        if (!strcmp(l->path, s->path)) { spinlock_release_irqrestore(&g_ulock, f); return -EADDRINUSE; }
    s->state = U_LISTEN;
    s->next = g_listeners;
    g_listeners = s;
    spinlock_release_irqrestore(&g_ulock, f);
    return 0;
}

int64_t unix_op_connect(vnode_t *vn, const char *path) {
    unix_sock_t *c = vn->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    if (c->state != U_UNBOUND && c->state != U_BOUND) { spinlock_release_irqrestore(&g_ulock, f); return -EINVAL; }
    unix_sock_t *l = g_listeners;
    while (l && strcmp(l->path, path)) l = l->next;
    if (!l || l->aqc >= UNIX_ACCEPTQ) { spinlock_release_irqrestore(&g_ulock, f); return -ECONNREFUSED; }

    unix_sock_t *srv = NULL;
    vnode_t *svn = unix_make(SOCK_STREAM, &srv);
    if (!svn) { spinlock_release_irqrestore(&g_ulock, f); return -ENOMEM; }
    srv->state = U_CONNECTED;
    srv->peer = c;
    c->peer = srv;
    c->state = U_CONNECTED;

    l->accept_q[l->aqt] = svn;
    l->aqt = (l->aqt + 1) % UNIX_ACCEPTQ;
    l->aqc++;
    task_t *acc = l->acceptor;
    l->acceptor = NULL;
    spinlock_release_irqrestore(&g_ulock, f);
    if (acc) task_unblock(acc);
    return 0;
}

vnode_t *unix_op_accept(vnode_t *vn, int nonblock) {
    unix_sock_t *l = vn->fs_data;
    task_t *me = syscall_cur_task();
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&g_ulock);
        if (l->state != U_LISTEN) { spinlock_release_irqrestore(&g_ulock, f); return NULL; }
        if (l->aqc > 0) {
            vnode_t *svn = l->accept_q[l->aqh];
            l->aqh = (l->aqh + 1) % UNIX_ACCEPTQ;
            l->aqc--;
            spinlock_release_irqrestore(&g_ulock, f);
            return svn;
        }
        if (nonblock) { spinlock_release_irqrestore(&g_ulock, f); return NULL; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&g_ulock, f); return NULL; }
        if (me) { l->acceptor = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&g_ulock, f);
        if (me) sched_reschedule(); else task_yield();
    }
}

static int64_t unix_read_op(vnode_t *vn, void *buf, size_t len, uint64_t off) {
    (void)off;
    unix_sock_t *s = vn->fs_data;
    task_t *me = syscall_cur_task();
    uint8_t *dst = buf;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&g_ulock);
        uint32_t avail = rb_count(s);
        if (avail > 0) {
            size_t n = avail < len ? avail : len;
            for (size_t i = 0; i < n; i++) { dst[i] = s->rbuf[s->rhead]; s->rhead = (s->rhead + 1) % UNIX_BUFSZ; }
            unix_sock_t *peer = s->peer;
            spinlock_release_irqrestore(&g_ulock, f);
            (void)peer;
            return (int64_t)n;
        }
        if (s->peer_gone) { spinlock_release_irqrestore(&g_ulock, f); return 0; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&g_ulock, f); return -EINTR; }
        if (me) { s->reader = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&g_ulock, f);
        if (me) sched_reschedule(); else task_yield();
    }
}

static int64_t unix_write_op(vnode_t *vn, const void *buf, size_t len, uint64_t off) {
    (void)off;
    unix_sock_t *s = vn->fs_data;
    task_t *me = syscall_cur_task();
    const uint8_t *src = buf;
    size_t done = 0;
    while (done < len) {
        uint64_t f = spinlock_acquire_irqsave(&g_ulock);
        unix_sock_t *peer = s->peer;
        if (!peer || s->peer_gone) { spinlock_release_irqrestore(&g_ulock, f); return done ? (int64_t)done : -EPIPE; }
        uint32_t space = rb_space(peer);
        if (space == 0) {
            if (me && me->pending_kill) { spinlock_release_irqrestore(&g_ulock, f); return done ? (int64_t)done : -EINTR; }
            if (done > 0) { spinlock_release_irqrestore(&g_ulock, f); return (int64_t)done; }
            spinlock_release_irqrestore(&g_ulock, f);
            task_sleep_ms(2);
            continue;
        }
        size_t n = len - done;
        if (n > space) n = space;
        for (size_t i = 0; i < n; i++) { peer->rbuf[peer->rtail] = src[done + i]; peer->rtail = (peer->rtail + 1) % UNIX_BUFSZ; }
        done += n;
        task_t *r = peer->reader;
        peer->reader = NULL;
        spinlock_release_irqrestore(&g_ulock, f);
        if (r) task_unblock(r);
    }
    return (int64_t)done;
}

int64_t unix_send_fd(vnode_t *vn, vfs_file_t *file) {
    unix_sock_t *s = vn->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    unix_sock_t *peer = s->peer;
    if (!peer || s->peer_gone) { spinlock_release_irqrestore(&g_ulock, f); return -EPIPE; }
    if (peer->fic >= 8) { spinlock_release_irqrestore(&g_ulock, f); return -EAGAIN; }
    peer->fd_inbox[peer->fit] = file;
    peer->fit = (peer->fit + 1) % 8;
    peer->fic++;
    task_t *w = peer->fd_waiter;
    peer->fd_waiter = NULL;
    spinlock_release_irqrestore(&g_ulock, f);
    if (w) task_unblock(w);
    return 0;
}

vfs_file_t *unix_recv_fd(vnode_t *vn, int nonblock) {
    unix_sock_t *s = vn->fs_data;
    task_t *me = syscall_cur_task();
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&g_ulock);
        if (s->fic > 0) {
            vfs_file_t *file = s->fd_inbox[s->fih];
            s->fih = (s->fih + 1) % 8;
            s->fic--;
            spinlock_release_irqrestore(&g_ulock, f);
            return file;
        }
        if (s->peer_gone) { spinlock_release_irqrestore(&g_ulock, f); return NULL; }
        if (nonblock) { spinlock_release_irqrestore(&g_ulock, f); return NULL; }
        if (me && me->pending_kill) { spinlock_release_irqrestore(&g_ulock, f); return NULL; }
        if (me) { s->fd_waiter = me; me->runnable = false; me->state = TASK_BLOCKED; }
        spinlock_release_irqrestore(&g_ulock, f);
        if (me) sched_reschedule(); else task_yield();
    }
}

static int unix_poll_op(vnode_t *vn, int events) {
    (void)events;
    unix_sock_t *s = vn->fs_data;
    int r = 0;
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    if (s->state == U_LISTEN) {
        if (s->aqc > 0) r |= POLLIN;
    } else {
        if (rb_count(s) > 0) r |= POLLIN;
        if (s->peer_gone) r |= POLLIN | POLLHUP;
        if (s->peer && !s->peer_gone && rb_space(s->peer) > 0) r |= POLLOUT;
    }
    spinlock_release_irqrestore(&g_ulock, f);
    return r;
}

static void unix_ref_op(vnode_t *vn) { (void)vn; }

static void unix_unref_op(vnode_t *vn) {
    unix_sock_t *s = vn->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);

    while (s->fic > 0) {
        vfs_file_t *pf = s->fd_inbox[s->fih];
        s->fih = (s->fih + 1) % 8;
        s->fic--;
        spinlock_release_irqrestore(&g_ulock, f);
        fd_put(pf);
        f = spinlock_acquire_irqsave(&g_ulock);
    }

    if (s->state == U_LISTEN) {
        unix_sock_t **pp = &g_listeners;
        while (*pp) { if (*pp == s) { *pp = s->next; break; } pp = &(*pp)->next; }
        for (int i = 0; i < s->aqc; i++) {
            vnode_t *svn = s->accept_q[(s->aqh + i) % UNIX_ACCEPTQ];
            unix_sock_t *ss = svn->fs_data;
            ss->peer_gone = 1;
            if (ss->peer) ss->peer->peer_gone = 1;
        }
    }

    unix_sock_t *peer = s->peer;
    if (peer) {
        peer->peer_gone = 1;
        peer->peer = NULL;
        task_t *r = peer->reader;
        peer->reader = NULL;
        spinlock_release_irqrestore(&g_ulock, f);
        if (r) task_unblock(r);
    } else {
        spinlock_release_irqrestore(&g_ulock, f);
    }

    free(s);
    free(vn);
}

static const vnode_ops_t unix_vnode_ops = {
    .read  = unix_read_op,
    .write = unix_write_op,
    .ref   = unix_ref_op,
    .unref = unix_unref_op,
    .poll  = unix_poll_op,
};
