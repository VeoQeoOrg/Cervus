#include "../../include/net/socket.h"
#include "../../include/fs/vfs.h"
#include "../../include/fs/poll.h"
#include "../../include/io/serial.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/spinlock.h"
#include "../../include/sched/waitq.h"
#include "../../include/signal/signal.h"
#include "../../include/drivers/timer.h"
#include "../../include/syscall/syscall_internal.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define UNIX_BUF_DEF    212992u
#define UNIX_BUF_MAX    212992u
#define UNIX_MIN_RCV    2304u
#define UNIX_MIN_SND    4608u
#define UNIX_SEG_MAX    65536u
#define UNIX_DGRAM_QLEN 512
#define UNIX_SOMAXCONN  4096
#define UNIX_FDQ_MAX    64

#define FIONREAD_K 0x541B
#define TIOCOUTQ_K 0x5411

enum { U_UNCONN, U_LISTEN, U_CONNECTED };

#define SHUT_RCV 1
#define SHUT_SND 2
#define SHUT_ALL 3

typedef struct useg {
    struct useg *next;
    uint32_t     len, off;
    uint32_t     pid, uid, gid;
    int          nfds;
    vfs_file_t **fds;
    uint32_t     from_len;
    char         from[UNIX_PATH_MAX];
    uint8_t      data[];
} useg_t;

typedef struct ufd {
    struct ufd *next;
    vfs_file_t *file;
} ufd_t;

typedef struct unix_sock {
    int        type;
    int        state;
    int        refs;
    int        dead;
    int        shut;
    int        err;
    int        passcred;
    uint32_t   sndbuf, rcvbuf;
    int64_t    rcvtimeo_ms, sndtimeo_ms;

    uint32_t   name_len;
    char       name[UNIX_PATH_MAX];
    int        bound;
    uintptr_t  key_fs;
    uint64_t   key_id;
    vnode_t   *key_node;

    struct unix_sock *peer;

    spinlock_t lock;
    waitq_t    wq;
    useg_t    *qhead, *qtail;
    uint32_t   qbytes;
    int        qlen;
    ufd_t     *fdq_head, *fdq_tail;
    int        fdq_len;

    struct unix_sock *aq_head, *aq_tail, *aq_next;
    int        aqlen, backlog;

    uint32_t   cred_pid, cred_uid, cred_gid;
    uint32_t   peer_pid, peer_uid, peer_gid;
    int        has_peercred;

    struct unix_sock *bnext;
} unix_sock_t;

static const vnode_ops_t unix_vnode_ops;
static unix_sock_t *g_bound;
static spinlock_t   g_ulock = SPINLOCK_INIT;
static uint32_t     g_autobind = 1;
static uint64_t     g_unix_ino = 0x30000;

int unix_is_vnode(const vnode_t *vn) { return vn && vn->ops == &unix_vnode_ops; }

int unix_sock_type(const vnode_t *vn) {
    unix_sock_t *s = vn->fs_data;
    return s ? s->type : SOCK_STREAM;
}

static int interrupted(task_t *me) {
    return me && (me->pending_kill || signal_pending_deliverable(me));
}

static uint64_t deadline_of(int64_t ms) {
    return ms > 0 ? sched_now_ns() + (uint64_t)ms * 1000000ull : 0;
}

static int expired(uint64_t deadline) {
    return deadline && sched_now_ns() >= deadline;
}

static void seg_free(useg_t *g) {
    for (int i = 0; i < g->nfds; i++) if (g->fds[i]) fd_put(g->fds[i]);
    free(g->fds);
    free(g);
}

static void seg_list_free(useg_t *g) {
    while (g) { useg_t *n = g->next; seg_free(g); g = n; }
}

static void us_get(unix_sock_t *s) {
    __atomic_add_fetch(&s->refs, 1, __ATOMIC_ACQ_REL);
}

static void us_put(unix_sock_t *s) {
    if (!s || __atomic_sub_fetch(&s->refs, 1, __ATOMIC_ACQ_REL) != 0) return;
    seg_list_free(s->qhead);
    ufd_t *u = s->fdq_head;
    while (u) { ufd_t *n = u->next; fd_put(u->file); free(u); u = n; }
    free(s);
}

static void cur_creds(uint32_t *pid, uint32_t *uid, uint32_t *gid) {
    task_t *t = syscall_cur_task();
    *pid = t ? t->pid : 0;
    *uid = t ? t->uid : 0;
    *gid = t ? t->gid : 0;
}

static unix_sock_t *us_new(int type) {
    unix_sock_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->type = type;
    s->state = U_UNCONN;
    s->refs = 1;
    s->sndbuf = UNIX_BUF_DEF;
    s->rcvbuf = UNIX_BUF_DEF;
    cur_creds(&s->cred_pid, &s->cred_uid, &s->cred_gid);
    return s;
}

static vnode_t *us_wrap(unix_sock_t *s) {
    vnode_t *vn = calloc(1, sizeof(*vn));
    if (!vn) return NULL;
    vn->type = VFS_NODE_SOCKET;
    vn->mode = 0777;
    vn->ino = __atomic_fetch_add(&g_unix_ino, 1, __ATOMIC_RELAXED);
    vn->ops = &unix_vnode_ops;
    vn->fs_data = s;
    vn->refcount = 1;
    return vn;
}

vnode_t *unix_new_vnode(int type) {
    if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_SEQPACKET) return NULL;
    unix_sock_t *s = us_new(type);
    if (!s) return NULL;
    vnode_t *vn = us_wrap(s);
    if (!vn) { free(s); return NULL; }
    return vn;
}

int unix_make_pair(int type, vnode_t **a_out, vnode_t **b_out) {
    if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_SEQPACKET) return -EPROTONOSUPPORT;
    unix_sock_t *a = us_new(type), *b = us_new(type);
    vnode_t *va = a ? us_wrap(a) : NULL;
    vnode_t *vb = b ? us_wrap(b) : NULL;
    if (!va || !vb) {
        free(va); free(vb); free(a); free(b);
        return -ENOMEM;
    }
    a->peer = b; us_get(b);
    b->peer = a; us_get(a);
    a->state = b->state = U_CONNECTED;
    a->peer_pid = b->peer_pid = a->cred_pid;
    a->peer_uid = b->peer_uid = a->cred_uid;
    a->peer_gid = b->peer_gid = a->cred_gid;
    a->has_peercred = b->has_peercred = 1;
    *a_out = va;
    *b_out = vb;
    return 0;
}

static int is_abstract(const sock_addr_t *a) {
    return a->ulen > 0 && a->upath[0] == 0;
}

static int abs_path(const sock_addr_t *a, char *out, size_t cap) {
    char rel[UNIX_PATH_MAX + 1];
    uint32_t n = a->ulen > UNIX_PATH_MAX ? UNIX_PATH_MAX : a->ulen;
    memcpy(rel, a->upath, n);
    rel[n] = 0;
    if (!rel[0]) return -ENOENT;
    return syscall_resolve_path(out, rel, cap);
}

static void node_key(vnode_t *n, uintptr_t *kfs, uint64_t *kid) {
    *kfs = (uintptr_t)n;
    *kid = 0;
    if (n->ops && n->ops->cache_id) {
        uintptr_t fs = 0;
        uint64_t id = 0;
        if (n->ops->cache_id(n, &fs, &id) == 0) { *kfs = fs; *kid = id; }
    }
}

static unix_sock_t *find_abstract_locked(const sock_addr_t *a) {
    for (unix_sock_t *s = g_bound; s; s = s->bnext)
        if (!s->key_node && s->name_len == a->ulen && memcmp(s->name, a->upath, a->ulen) == 0)
            return s;
    return NULL;
}

static unix_sock_t *find_key_locked(uintptr_t kfs, uint64_t kid) {
    for (unix_sock_t *s = g_bound; s; s = s->bnext)
        if (s->key_node && s->key_fs == kfs && s->key_id == kid) return s;
    return NULL;
}

static int64_t lookup_target(const sock_addr_t *a, unix_sock_t **out) {
    *out = NULL;
    if (a->family != AF_UNIX) return -EINVAL;
    if (a->ulen == 0) return -EINVAL;
    if (is_abstract(a)) {
        uint64_t f = spinlock_acquire_irqsave(&g_ulock);
        unix_sock_t *t = find_abstract_locked(a);
        if (t) us_get(t);
        spinlock_release_irqrestore(&g_ulock, f);
        if (!t) return -ECONNREFUSED;
        *out = t;
        return 0;
    }
    char kpath[VFS_MAX_PATH];
    int r = abs_path(a, kpath, sizeof kpath);
    if (r < 0) return r;
    vnode_t *n = NULL;
    r = vfs_lookup(kpath, &n);
    if (r < 0) return r;
    if (n->type != VFS_NODE_SOCKET) { vnode_unref(n); return -ECONNREFUSED; }
    r = syscall_perm_file(kpath, 2);
    if (r < 0) { vnode_unref(n); return r; }
    uintptr_t kfs; uint64_t kid;
    node_key(n, &kfs, &kid);
    vnode_unref(n);
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    unix_sock_t *t = find_key_locked(kfs, kid);
    if (t) us_get(t);
    spinlock_release_irqrestore(&g_ulock, f);
    if (!t) return -ECONNREFUSED;
    *out = t;
    return 0;
}

static int64_t autobind(unix_sock_t *s) {
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    if (s->bound) { spinlock_release_irqrestore(&g_ulock, f); return 0; }
    for (int tries = 0; tries < 0x100000; tries++) {
        sock_addr_t a;
        memset(&a, 0, sizeof a);
        a.family = AF_UNIX;
        a.upath[0] = 0;
        snprintf(a.upath + 1, sizeof a.upath - 1, "%05x", g_autobind & 0xFFFFF);
        g_autobind++;
        a.ulen = 6;
        if (find_abstract_locked(&a)) continue;
        memcpy(s->name, a.upath, a.ulen);
        s->name_len = a.ulen;
        s->bound = 1;
        s->bnext = g_bound;
        g_bound = s;
        spinlock_release_irqrestore(&g_ulock, f);
        return 0;
    }
    spinlock_release_irqrestore(&g_ulock, f);
    return -ENOSPC;
}

int64_t unix_op_bind(vnode_t *vn, const sock_addr_t *a) {
    unix_sock_t *s = vn->fs_data;
    if (a->family != AF_UNIX) return -EINVAL;
    if (s->bound) return -EINVAL;
    if (a->ulen == 0) return autobind(s);

    if (is_abstract(a)) {
        uint64_t f = spinlock_acquire_irqsave(&g_ulock);
        if (s->bound) { spinlock_release_irqrestore(&g_ulock, f); return -EINVAL; }
        if (find_abstract_locked(a)) { spinlock_release_irqrestore(&g_ulock, f); return -EADDRINUSE; }
        memcpy(s->name, a->upath, a->ulen);
        s->name_len = a->ulen;
        s->bound = 1;
        s->bnext = g_bound;
        g_bound = s;
        spinlock_release_irqrestore(&g_ulock, f);
        return 0;
    }

    char kpath[VFS_MAX_PATH];
    int r = abs_path(a, kpath, sizeof kpath);
    if (r < 0) return r;
    r = syscall_perm_parent(kpath, 3);
    if (r < 0) return r;
    task_t *t = syscall_cur_task();
    uint32_t mode = 0777u & ~(t ? (t->umask & 0777u) : 022u);
    r = vfs_mknod(kpath, 0140000u | mode, 0);
    if (r == -EEXIST) return -EADDRINUSE;
    if (r < 0) return r;
    vnode_t *n = NULL;
    r = vfs_lookup(kpath, &n);
    if (r < 0) return r;
    uintptr_t kfs; uint64_t kid;
    node_key(n, &kfs, &kid);

    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    if (s->bound) {
        spinlock_release_irqrestore(&g_ulock, f);
        vnode_unref(n);
        return -EINVAL;
    }
    uint32_t nl = 0;
    while (nl < a->ulen && a->upath[nl]) nl++;
    memcpy(s->name, a->upath, nl);
    s->name_len = nl;
    s->key_node = n;
    s->key_fs = kfs;
    s->key_id = kid;
    s->bound = 1;
    s->bnext = g_bound;
    g_bound = s;
    spinlock_release_irqrestore(&g_ulock, f);
    return 0;
}

int64_t unix_op_listen(vnode_t *vn, int backlog) {
    unix_sock_t *s = vn->fs_data;
    if (s->type == SOCK_DGRAM) return -EOPNOTSUPP;
    if (backlog < 0 || backlog > UNIX_SOMAXCONN) backlog = UNIX_SOMAXCONN;
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    if (s->state == U_CONNECTED || !s->bound) {
        spinlock_release_irqrestore(&g_ulock, f);
        return -EINVAL;
    }
    s->state = U_LISTEN;
    cur_creds(&s->cred_pid, &s->cred_uid, &s->cred_gid);
    spinlock_release_irqrestore(&g_ulock, f);
    uint64_t q = spinlock_acquire_irqsave(&s->lock);
    s->backlog = backlog;
    waitq_wake_all(&s->wq);
    spinlock_release_irqrestore(&s->lock, q);
    return 0;
}

static unix_sock_t *get_peer(unix_sock_t *s) {
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    unix_sock_t *p = s->peer;
    if (p) us_get(p);
    spinlock_release_irqrestore(&g_ulock, f);
    return p;
}

static int64_t connect_stream(unix_sock_t *s, const sock_addr_t *a, int nonblock) {
    if (s->state == U_CONNECTED) return -EISCONN;
    if (s->state == U_LISTEN) return -EINVAL;
    unix_sock_t *t = NULL;
    int64_t r = lookup_target(a, &t);
    if (r < 0) return r;
    if (t->type != s->type) { us_put(t); return -EPROTOTYPE; }

    unix_sock_t *child = us_new(s->type);
    if (!child) { us_put(t); return -ENOMEM; }
    if (s->passcred && !s->bound) autobind(s);

    task_t *me = syscall_cur_task();
    uint64_t deadline = deadline_of(s->sndtimeo_ms);
    for (;;) {
        uint64_t q = spinlock_acquire_irqsave(&t->lock);
        if (t->dead || t->state != U_LISTEN) {
            spinlock_release_irqrestore(&t->lock, q);
            r = -ECONNREFUSED;
            break;
        }
        if (t->aqlen > t->backlog) {
            if (nonblock)          { spinlock_release_irqrestore(&t->lock, q); r = -EAGAIN; break; }
            if (interrupted(me))   { spinlock_release_irqrestore(&t->lock, q); r = -EINTR; break; }
            if (expired(deadline)) { spinlock_release_irqrestore(&t->lock, q); r = -EAGAIN; break; }
            waitq_block(&t->wq, &t->lock, q, deadline);
            continue;
        }
        child->state = U_CONNECTED;
        child->peer = s;
        us_get(s);
        memcpy(child->name, t->name, t->name_len);
        child->name_len = t->name_len;
        child->sndbuf = t->sndbuf;
        child->rcvbuf = t->rcvbuf;
        child->passcred = t->passcred;
        child->cred_pid = t->cred_pid;
        child->cred_uid = t->cred_uid;
        child->cred_gid = t->cred_gid;
        cur_creds(&child->peer_pid, &child->peer_uid, &child->peer_gid);
        child->has_peercred = 1;
        if (t->aq_tail) t->aq_tail->aq_next = child; else t->aq_head = child;
        t->aq_tail = child;
        child->aq_next = NULL;
        t->aqlen++;
        uint32_t lp = t->cred_pid, lu = t->cred_uid, lg = t->cred_gid;
        waitq_wake_all(&t->wq);
        spinlock_release_irqrestore(&t->lock, q);

        uint64_t f = spinlock_acquire_irqsave(&g_ulock);
        s->peer = child;
        us_get(child);
        s->state = U_CONNECTED;
        s->peer_pid = lp;
        s->peer_uid = lu;
        s->peer_gid = lg;
        s->has_peercred = 1;
        spinlock_release_irqrestore(&g_ulock, f);
        us_put(t);
        return 0;
    }
    us_put(t);
    free(child);
    return r;
}

static int64_t connect_dgram(unix_sock_t *s, const sock_addr_t *a) {
    unix_sock_t *t = NULL;
    if (a->family == AF_UNSPEC) {
        uint64_t f = spinlock_acquire_irqsave(&g_ulock);
        unix_sock_t *old = s->peer;
        s->peer = NULL;
        s->state = U_UNCONN;
        spinlock_release_irqrestore(&g_ulock, f);
        us_put(old);
        return 0;
    }
    int64_t r = lookup_target(a, &t);
    if (r < 0) return r;
    if (t->type != s->type) { us_put(t); return -EPROTOTYPE; }
    if (s->passcred && !s->bound) autobind(s);
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    unix_sock_t *old = s->peer;
    s->peer = t;
    s->state = U_CONNECTED;
    s->peer_pid = t->cred_pid;
    s->peer_uid = t->cred_uid;
    s->peer_gid = t->cred_gid;
    s->has_peercred = 1;
    spinlock_release_irqrestore(&g_ulock, f);
    us_put(old);
    return 0;
}

int64_t unix_op_connect(vnode_t *vn, const sock_addr_t *a, int nonblock) {
    unix_sock_t *s = vn->fs_data;
    if (s->type == SOCK_DGRAM) return connect_dgram(s, a);
    if (a->family != AF_UNIX) return -EINVAL;
    return connect_stream(s, a, nonblock);
}

int64_t unix_op_accept(vnode_t *vn, int nonblock, vnode_t **out, sock_addr_t *peer) {
    unix_sock_t *s = vn->fs_data;
    if (s->type == SOCK_DGRAM) return -EOPNOTSUPP;
    task_t *me = syscall_cur_task();
    uint64_t deadline = deadline_of(s->rcvtimeo_ms);
    for (;;) {
        uint64_t q = spinlock_acquire_irqsave(&s->lock);
        if (s->state != U_LISTEN || s->dead) { spinlock_release_irqrestore(&s->lock, q); return -EINVAL; }
        unix_sock_t *c = s->aq_head;
        if (c) {
            s->aq_head = c->aq_next;
            if (!s->aq_head) s->aq_tail = NULL;
            c->aq_next = NULL;
            s->aqlen--;
            waitq_wake_all(&s->wq);
            spinlock_release_irqrestore(&s->lock, q);
            vnode_t *cvn = us_wrap(c);
            if (!cvn) {
                us_put(c);
                return -ENOMEM;
            }
            if (peer) {
                memset(peer, 0, sizeof(*peer));
                peer->family = AF_UNIX;
                unix_sock_t *p = get_peer(c);
                if (p) {
                    peer->ulen = p->name_len;
                    memcpy(peer->upath, p->name, p->name_len);
                    us_put(p);
                }
            }
            *out = cvn;
            return 0;
        }
        if (nonblock)          { spinlock_release_irqrestore(&s->lock, q); return -EAGAIN; }
        if (interrupted(me))   { spinlock_release_irqrestore(&s->lock, q); return -EINTR; }
        if (expired(deadline)) { spinlock_release_irqrestore(&s->lock, q); return -EAGAIN; }
        waitq_block(&s->wq, &s->lock, q, deadline);
    }
}

static useg_t *seg_new(unix_sock_t *s, const void *buf, uint32_t len, unix_anc_t *anc) {
    useg_t *g = malloc(sizeof(*g) + len);
    if (!g) return NULL;
    memset(g, 0, sizeof(*g));
    g->len = len;
    if (len) memcpy(g->data, buf, len);
    if (anc && anc->has_creds) {
        g->pid = anc->pid; g->uid = anc->uid; g->gid = anc->gid;
    } else {
        cur_creds(&g->pid, &g->uid, &g->gid);
    }
    if (anc && anc->nfds > 0) {
        g->fds = malloc(sizeof(vfs_file_t *) * (size_t)anc->nfds);
        if (!g->fds) { free(g); return NULL; }
        memcpy(g->fds, anc->fds, sizeof(vfs_file_t *) * (size_t)anc->nfds);
        g->nfds = anc->nfds;
        anc->nfds = 0;
    }
    if (s->bound) {
        memcpy(g->from, s->name, s->name_len);
        g->from_len = s->name_len;
    }
    return g;
}

static void anc_release(unix_anc_t *anc) {
    if (!anc) return;
    for (int i = 0; i < anc->nfds; i++) if (anc->fds[i]) fd_put(anc->fds[i]);
    anc->nfds = 0;
}

static void enqueue_locked(unix_sock_t *p, useg_t *g) {
    g->next = NULL;
    if (p->qtail) p->qtail->next = g; else p->qhead = g;
    p->qtail = g;
    p->qbytes += g->len;
    p->qlen++;
    waitq_wake_all(&p->wq);
}

static int64_t send_conn(unix_sock_t *s, const uint8_t *buf, size_t len, int flags, unix_anc_t *anc) {
    if (s->state != U_CONNECTED) { anc_release(anc); return -ENOTCONN; }
    if (s->shut & SHUT_SND) { anc_release(anc); return -EPIPE; }
    if (s->type == SOCK_SEQPACKET && len > s->sndbuf - 32) { anc_release(anc); return -EMSGSIZE; }
    if (s->type == SOCK_STREAM && len == 0) { anc_release(anc); return 0; }
    unix_sock_t *p = get_peer(s);
    if (!p) { anc_release(anc); return -EPIPE; }

    task_t *me = syscall_cur_task();
    uint64_t deadline = deadline_of(s->sndtimeo_ms);
    int nonblock = (flags & MSG_DONTWAIT) != 0;
    size_t done = 0;
    int64_t err = 0;
    useg_t *g = NULL;
    do {
        if (!g) {
            uint32_t chunk = (uint32_t)(len - done);
            if (s->type == SOCK_STREAM && chunk > UNIX_SEG_MAX) chunk = UNIX_SEG_MAX;
            g = seg_new(s, buf + done, chunk, anc);
            if (!g) { err = -ENOBUFS; break; }
        }
        uint64_t q = spinlock_acquire_irqsave(&p->lock);
        if (p->dead || (p->shut & SHUT_RCV)) { spinlock_release_irqrestore(&p->lock, q); err = -EPIPE; break; }
        if (p->qbytes >= s->sndbuf) {
            if (nonblock)          { spinlock_release_irqrestore(&p->lock, q); err = -EAGAIN; break; }
            if (interrupted(me))   { spinlock_release_irqrestore(&p->lock, q); err = -EINTR; break; }
            if (expired(deadline)) { spinlock_release_irqrestore(&p->lock, q); err = -EAGAIN; break; }
            waitq_block(&p->wq, &p->lock, q, deadline);
            continue;
        }
        done += g->len;
        enqueue_locked(p, g);
        spinlock_release_irqrestore(&p->lock, q);
        g = NULL;
    } while (done < len);
    if (g) seg_free(g);
    anc_release(anc);
    us_put(p);
    if (done) return (int64_t)done;
    if (len == 0 && !err) return 0;
    return err;
}

static int64_t send_dgram(unix_sock_t *s, const uint8_t *buf, size_t len, int flags,
                          const sock_addr_t *to, unix_anc_t *anc) {
    if (s->shut & SHUT_SND) { anc_release(anc); return -EPIPE; }
    if (len > s->sndbuf - 32) { anc_release(anc); return -EMSGSIZE; }
    if (s->passcred && !s->bound) autobind(s);

    unix_sock_t *t = NULL;
    int connected_target = 0;
    if (to && to->family != AF_UNSPEC) {
        int64_t r = lookup_target(to, &t);
        if (r < 0) { anc_release(anc); return r; }
    } else {
        t = get_peer(s);
        if (!t) { anc_release(anc); return -ENOTCONN; }
        connected_target = 1;
    }
    if (t->type != s->type) { us_put(t); anc_release(anc); return -EPROTOTYPE; }

    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    int may = !t->peer || t->peer == s;
    spinlock_release_irqrestore(&g_ulock, f);
    if (!may) { us_put(t); anc_release(anc); return -EPERM; }

    useg_t *g = seg_new(s, buf, (uint32_t)len, anc);
    anc_release(anc);
    if (!g) { us_put(t); return -ENOBUFS; }

    task_t *me = syscall_cur_task();
    uint64_t deadline = deadline_of(s->sndtimeo_ms);
    int64_t r;
    for (;;) {
        uint64_t q = spinlock_acquire_irqsave(&t->lock);
        if (t->dead) {
            spinlock_release_irqrestore(&t->lock, q);
            if (connected_target) {
                uint64_t gf = spinlock_acquire_irqsave(&g_ulock);
                unix_sock_t *old = s->peer == t ? t : NULL;
                if (old) { s->peer = NULL; s->state = U_UNCONN; }
                spinlock_release_irqrestore(&g_ulock, gf);
                us_put(old);
            }
            r = -ECONNREFUSED;
            break;
        }
        if (t->shut & SHUT_RCV) { spinlock_release_irqrestore(&t->lock, q); r = -EPIPE; break; }
        if (t->qlen >= UNIX_DGRAM_QLEN || (t->qhead && t->qbytes + len > t->rcvbuf)) {
            if (flags & MSG_DONTWAIT) { spinlock_release_irqrestore(&t->lock, q); r = -EAGAIN; break; }
            if (interrupted(me))      { spinlock_release_irqrestore(&t->lock, q); r = -EINTR; break; }
            if (expired(deadline))    { spinlock_release_irqrestore(&t->lock, q); r = -EAGAIN; break; }
            waitq_block(&t->wq, &t->lock, q, deadline);
            continue;
        }
        enqueue_locked(t, g);
        spinlock_release_irqrestore(&t->lock, q);
        g = NULL;
        r = (int64_t)len;
        break;
    }
    if (g) seg_free(g);
    us_put(t);
    return r;
}

int64_t unix_op_sendmsg(vnode_t *vn, const void *buf, size_t len, int flags,
                        const sock_addr_t *to, unix_anc_t *anc) {
    unix_sock_t *s = vn->fs_data;
    if (flags & MSG_OOB) { anc_release(anc); return -EOPNOTSUPP; }
    if (s->type == SOCK_DGRAM) return send_dgram(s, buf, len, flags, to, anc);
    if (s->type == SOCK_STREAM && to && to->family != AF_UNSPEC) {
        anc_release(anc);
        return s->state == U_CONNECTED ? -EISCONN : -EOPNOTSUPP;
    }
    return send_conn(s, buf, len, flags, anc);
}

static void deliver_seg_extras(unix_sock_t *s, useg_t *g, sock_addr_t *from, unix_anc_t *anc,
                               int *oflags, vfs_file_t ***drop, int *ndrop) {
    if (from) {
        memset(from, 0, sizeof(*from));
        from->family = AF_UNIX;
        from->ulen = g->from_len;
        memcpy(from->upath, g->from, g->from_len);
    }
    if (anc && s->passcred) {
        anc->has_creds = 1;
        anc->pid = g->pid; anc->uid = g->uid; anc->gid = g->gid;
    }
    if (g->nfds) {
        if (anc) {
            int room = UNIX_MAX_FDS - anc->nfds;
            int n = g->nfds < room ? g->nfds : room;
            memcpy(anc->fds + anc->nfds, g->fds, sizeof(vfs_file_t *) * (size_t)n);
            anc->nfds += n;
            for (int i = 0; i < n; i++) g->fds[i] = NULL;
            if (n < g->nfds && oflags) *oflags |= MSG_CTRUNC;
        }
        *drop = g->fds;
        *ndrop = g->nfds;
        g->fds = NULL;
        g->nfds = 0;
    }
}

int64_t unix_op_recvmsg(vnode_t *vn, void *buf, size_t len, int flags,
                        sock_addr_t *from, unix_anc_t *anc, int *oflags) {
    unix_sock_t *s = vn->fs_data;
    if (flags & MSG_OOB) return -EOPNOTSUPP;
    if (s->type != SOCK_DGRAM && s->state != U_CONNECTED) return -EINVAL;

    task_t *me = syscall_cur_task();
    uint64_t deadline = deadline_of(s->rcvtimeo_ms);
    uint8_t *dst = buf;
    size_t got = 0;
    size_t target = (s->type == SOCK_STREAM && (flags & MSG_WAITALL) && !(flags & MSG_PEEK)) ? len : 1;
    useg_t *freed = NULL;
    vfs_file_t **drop = NULL;
    int ndrop = 0;
    int64_t r;

    for (;;) {
        uint64_t q = spinlock_acquire_irqsave(&s->lock);
        useg_t *g = s->qhead;
        if (g && s->type == SOCK_STREAM) {
            if (flags & MSG_PEEK) {
                size_t n = 0;
                for (useg_t *x = g; x && n < len; x = x->next) {
                    uint32_t avail = x->len - x->off;
                    uint32_t c = avail < len - n ? avail : (uint32_t)(len - n);
                    memcpy(dst + n, x->data + x->off, c);
                    n += c;
                }
                if (from) {
                    memset(from, 0, sizeof(*from));
                    from->family = AF_UNIX;
                    from->ulen = g->from_len;
                    memcpy(from->upath, g->from, g->from_len);
                }
                spinlock_release_irqrestore(&s->lock, q);
                r = (int64_t)n;
                break;
            }
            int stop = 0;
            while (g && got < len && !stop) {
                int first = got == 0;
                uint32_t avail = g->len - g->off;
                uint32_t c = avail < len - got ? avail : (uint32_t)(len - got);
                memcpy(dst + got, g->data + g->off, c);
                got += c;
                g->off += c;
                s->qbytes -= c;
                if (first || g->nfds) deliver_seg_extras(s, g, first ? from : NULL, anc, oflags, &drop, &ndrop);
                if (drop) stop = 1;
                if (g->off == g->len) {
                    s->qhead = g->next;
                    if (!s->qhead) s->qtail = NULL;
                    s->qlen--;
                    g->next = freed;
                    freed = g;
                }
                g = s->qhead;
            }
            waitq_wake_all(&s->wq);
            if (got >= target || stop) {
                spinlock_release_irqrestore(&s->lock, q);
                r = (int64_t)got;
                break;
            }
            spinlock_release_irqrestore(&s->lock, q);
            continue;
        }
        if (g) {
            uint32_t real = g->len;
            size_t n = real < len ? real : len;
            memcpy(dst, g->data, n);
            if (real > len && oflags) *oflags |= MSG_TRUNC;
            if (flags & MSG_PEEK) {
                if (from) {
                    memset(from, 0, sizeof(*from));
                    from->family = AF_UNIX;
                    from->ulen = g->from_len;
                    memcpy(from->upath, g->from, g->from_len);
                }
            } else {
                deliver_seg_extras(s, g, from, anc, oflags, &drop, &ndrop);
                s->qhead = g->next;
                if (!s->qhead) s->qtail = NULL;
                s->qbytes -= real;
                s->qlen--;
                g->next = freed;
                freed = g;
                waitq_wake_all(&s->wq);
            }
            spinlock_release_irqrestore(&s->lock, q);
            r = (flags & MSG_TRUNC) ? (int64_t)real : (int64_t)n;
            break;
        }

        if (s->err && !got) {
            int e = s->err;
            s->err = 0;
            spinlock_release_irqrestore(&s->lock, q);
            r = -e;
            break;
        }
        if (s->shut & SHUT_RCV)   { spinlock_release_irqrestore(&s->lock, q); r = (int64_t)got; break; }
        if (flags & MSG_DONTWAIT) { spinlock_release_irqrestore(&s->lock, q); r = got ? (int64_t)got : -EAGAIN; break; }
        if (interrupted(me))      { spinlock_release_irqrestore(&s->lock, q); r = got ? (int64_t)got : -EINTR; break; }
        if (expired(deadline))    { spinlock_release_irqrestore(&s->lock, q); r = got ? (int64_t)got : -EAGAIN; break; }
        waitq_block(&s->wq, &s->lock, q, deadline);
    }

    seg_list_free(freed);
    if (drop) {
        for (int i = 0; i < ndrop; i++) if (drop[i]) fd_put(drop[i]);
        free(drop);
    }
    return r;
}

int64_t unix_op_shutdown(vnode_t *vn, int how) {
    unix_sock_t *s = vn->fs_data;
    if (how < 0 || how > 2) return -EINVAL;
    if (s->type != SOCK_DGRAM && s->state != U_CONNECTED) return -ENOTCONN;
    int mode = how == 0 ? SHUT_RCV : how == 1 ? SHUT_SND : SHUT_ALL;
    uint64_t q = spinlock_acquire_irqsave(&s->lock);
    s->shut |= mode;
    waitq_wake_all(&s->wq);
    spinlock_release_irqrestore(&s->lock, q);
    if (s->type == SOCK_DGRAM) return 0;
    unix_sock_t *p = get_peer(s);
    if (p) {
        int pm = 0;
        if (mode & SHUT_RCV) pm |= SHUT_SND;
        if (mode & SHUT_SND) pm |= SHUT_RCV;
        q = spinlock_acquire_irqsave(&p->lock);
        p->shut |= pm;
        waitq_wake_all(&p->wq);
        spinlock_release_irqrestore(&p->lock, q);
        us_put(p);
    }
    return 0;
}

int64_t unix_send_fd(vnode_t *vn, vfs_file_t *file) {
    unix_sock_t *s = vn->fs_data;
    unix_sock_t *p = get_peer(s);
    if (!p) return -ENOTCONN;
    ufd_t *u = malloc(sizeof(*u));
    if (!u) { us_put(p); return -ENOMEM; }
    u->file = file;
    u->next = NULL;
    uint64_t q = spinlock_acquire_irqsave(&p->lock);
    if (p->dead || (p->shut & SHUT_RCV)) {
        spinlock_release_irqrestore(&p->lock, q);
        free(u);
        us_put(p);
        return -EPIPE;
    }
    if (p->fdq_len >= UNIX_FDQ_MAX) {
        spinlock_release_irqrestore(&p->lock, q);
        free(u);
        us_put(p);
        return -EAGAIN;
    }
    if (p->fdq_tail) p->fdq_tail->next = u; else p->fdq_head = u;
    p->fdq_tail = u;
    p->fdq_len++;
    waitq_wake_all(&p->wq);
    spinlock_release_irqrestore(&p->lock, q);
    us_put(p);
    return 0;
}

vfs_file_t *unix_recv_fd(vnode_t *vn, int nonblock) {
    unix_sock_t *s = vn->fs_data;
    task_t *me = syscall_cur_task();
    for (;;) {
        uint64_t q = spinlock_acquire_irqsave(&s->lock);
        ufd_t *u = s->fdq_head;
        if (u) {
            s->fdq_head = u->next;
            if (!s->fdq_head) s->fdq_tail = NULL;
            s->fdq_len--;
            spinlock_release_irqrestore(&s->lock, q);
            vfs_file_t *f = u->file;
            free(u);
            return f;
        }
        if ((s->shut & SHUT_RCV) || nonblock || interrupted(me)) {
            spinlock_release_irqrestore(&s->lock, q);
            return NULL;
        }
        waitq_block(&s->wq, &s->lock, q, 0);
    }
}

static int64_t unix_read_op(vnode_t *vn, void *buf, size_t len, uint64_t off) {
    (void)off;
    return unix_op_recvmsg(vn, buf, len, vfs_io_nonblock() ? MSG_DONTWAIT : 0, NULL, NULL, NULL);
}

static int64_t unix_write_op(vnode_t *vn, const void *buf, size_t len, uint64_t off) {
    (void)off;
    int64_t r = unix_op_sendmsg(vn, buf, len, vfs_io_nonblock() ? MSG_DONTWAIT : 0, NULL, NULL);
    if (r == -EPIPE) sched_signal_current(SIGPIPE);
    return r;
}

static int unix_poll_op(vnode_t *vn, int events) {
    (void)events;
    unix_sock_t *s = vn->fs_data;
    int r = 0;
    uint64_t q = spinlock_acquire_irqsave(&s->lock);
    if (s->state == U_LISTEN) {
        if (s->aq_head) r |= POLLIN;
        spinlock_release_irqrestore(&s->lock, q);
        return r;
    }
    if (s->err) r |= POLLERR;
    if (s->shut == SHUT_ALL) r |= POLLHUP;
    if (s->shut & SHUT_RCV) r |= POLLIN | POLLRDHUP;
    if (s->qhead || s->fdq_head) r |= POLLIN;
    if (s->type != SOCK_DGRAM && s->state == U_UNCONN) r |= POLLHUP;
    int shut_snd = s->shut & SHUT_SND;
    spinlock_release_irqrestore(&s->lock, q);

    unix_sock_t *p = shut_snd ? NULL : get_peer(s);
    if (shut_snd) {
        r |= POLLOUT;
    } else if (p) {
        q = spinlock_acquire_irqsave(&p->lock);
        if (p->dead || (p->shut & SHUT_RCV)) r |= POLLOUT;
        else if (s->type == SOCK_DGRAM) { if (p->qlen < UNIX_DGRAM_QLEN) r |= POLLOUT; }
        else if ((uint64_t)p->qbytes * 4 <= s->sndbuf) r |= POLLOUT;
        spinlock_release_irqrestore(&p->lock, q);
        us_put(p);
    } else {
        r |= POLLOUT;
    }
    return r;
}

static int unix_stat_op(vnode_t *vn, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino = vn->ino;
    out->st_type = VFS_NODE_SOCKET;
    out->st_mode = 0140777;
    return 0;
}

static int64_t unix_ioctl_op(vnode_t *vn, uint64_t req, void *arg) {
    unix_sock_t *s = vn->fs_data;
    if (req == FIONREAD_K) {
        if (!arg) return -EFAULT;
        if (s->state == U_LISTEN) return -EINVAL;
        uint64_t q = spinlock_acquire_irqsave(&s->lock);
        int n = s->type == SOCK_STREAM ? (int)s->qbytes : (s->qhead ? (int)s->qhead->len : 0);
        spinlock_release_irqrestore(&s->lock, q);
        *(int *)arg = n;
        return 0;
    }
    if (req == TIOCOUTQ_K) {
        if (!arg) return -EFAULT;
        int n = 0;
        unix_sock_t *p = s->type == SOCK_DGRAM ? NULL : get_peer(s);
        if (p) {
            uint64_t q = spinlock_acquire_irqsave(&p->lock);
            n = (int)p->qbytes;
            spinlock_release_irqrestore(&p->lock, q);
            us_put(p);
        }
        *(int *)arg = n;
        return 0;
    }
    return -ENOTTY;
}

static void unix_ref_op(vnode_t *vn) { (void)vn; }

static void release_embryo(unix_sock_t *c) {
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    unix_sock_t *client = c->peer;
    c->peer = NULL;
    spinlock_release_irqrestore(&g_ulock, f);
    uint64_t q = spinlock_acquire_irqsave(&c->lock);
    c->dead = 1;
    c->shut = SHUT_ALL;
    spinlock_release_irqrestore(&c->lock, q);
    if (client) {
        q = spinlock_acquire_irqsave(&client->lock);
        client->shut = SHUT_ALL;
        client->err = ECONNRESET;
        waitq_wake_all(&client->wq);
        spinlock_release_irqrestore(&client->lock, q);
        us_put(client);
    }
    us_put(c);
}

static void unix_unref_op(vnode_t *vn) {
    unix_sock_t *s = vn->fs_data;

    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    if (s->bound) {
        unix_sock_t **pp = &g_bound;
        while (*pp) { if (*pp == s) { *pp = s->bnext; break; } pp = &(*pp)->bnext; }
        s->bound = 0;
    }
    vnode_t *key = s->key_node;
    s->key_node = NULL;
    unix_sock_t *peer = s->peer;
    s->peer = NULL;
    spinlock_release_irqrestore(&g_ulock, f);

    uint64_t q = spinlock_acquire_irqsave(&s->lock);
    s->dead = 1;
    s->shut = SHUT_ALL;
    int unread = s->qhead != NULL;
    useg_t *segs = s->qhead;
    s->qhead = s->qtail = NULL;
    s->qbytes = 0;
    s->qlen = 0;
    unix_sock_t *emb = s->aq_head;
    s->aq_head = s->aq_tail = NULL;
    s->aqlen = 0;
    waitq_wake_all(&s->wq);
    spinlock_release_irqrestore(&s->lock, q);

    if (peer) {
        if (s->type != SOCK_DGRAM) {
            q = spinlock_acquire_irqsave(&peer->lock);
            peer->shut = SHUT_ALL;
            if (unread) peer->err = ECONNRESET;
            waitq_wake_all(&peer->wq);
            spinlock_release_irqrestore(&peer->lock, q);
        }
        us_put(peer);
    }
    while (emb) {
        unix_sock_t *n = emb->aq_next;
        emb->aq_next = NULL;
        release_embryo(emb);
        emb = n;
    }
    seg_list_free(segs);
    if (key) vnode_unref(key);
    free(vn);
    us_put(s);
}

static const vnode_ops_t unix_vnode_ops = {
    .read  = unix_read_op,
    .write = unix_write_op,
    .stat  = unix_stat_op,
    .ref   = unix_ref_op,
    .unref = unix_unref_op,
    .poll  = unix_poll_op,
    .ioctl = unix_ioctl_op,
};

int unix_describe(const vnode_t *vn, char *buf, size_t max) {
    if (!unix_is_vnode(vn)) return -1;
    unix_sock_t *s = vn->fs_data;
    uint64_t q = spinlock_acquire_irqsave(&s->lock);
    int n = snprintf(buf, max, "unix type=%d state=%d shut=%d queued=%u fds=%d",
                     s->type, s->state, s->shut, s->qbytes, s->fdq_len);
    spinlock_release_irqrestore(&s->lock, q);
    return n;
}

int unix_is_listening(const vnode_t *vn) {
    unix_sock_t *s = vn->fs_data;
    return s && s->state == U_LISTEN;
}

int unix_peer_cred(const vnode_t *vn, uint32_t *pid, uint32_t *uid, uint32_t *gid) {
    if (!unix_is_vnode(vn)) return -EINVAL;
    unix_sock_t *s = vn->fs_data;
    if (!s->has_peercred) return -ENOTCONN;
    if (pid) *pid = s->peer_pid;
    if (uid) *uid = s->peer_uid;
    if (gid) *gid = s->peer_gid;
    return 0;
}

int unix_get_name(const vnode_t *vn, int peer, sock_addr_t *out) {
    unix_sock_t *s = vn->fs_data;
    memset(out, 0, sizeof(*out));
    out->family = AF_UNIX;
    uint64_t f = spinlock_acquire_irqsave(&g_ulock);
    unix_sock_t *src = s;
    if (peer) {
        src = s->peer;
        if (!src) { spinlock_release_irqrestore(&g_ulock, f); return -ENOTCONN; }
    }
    out->ulen = src->name_len;
    memcpy(out->upath, src->name, src->name_len);
    spinlock_release_irqrestore(&g_ulock, f);
    return 0;
}

#define SOL_SOCKET_K     1
#define SO_REUSEADDR_K   2
#define SO_TYPE_K        3
#define SO_ERROR_K       4
#define SO_SNDBUF_K      7
#define SO_RCVBUF_K      8
#define SO_KEEPALIVE_K   9
#define SO_PASSCRED_K   16
#define SO_PEERCRED_K   17
#define SO_RCVTIMEO_K   20
#define SO_SNDTIMEO_K   21
#define SO_ACCEPTCONN_K 30
#define SO_SNDBUFFORCE_K 32
#define SO_RCVBUFFORCE_K 33
#define SO_PROTOCOL_K   38
#define SO_DOMAIN_K     39

typedef struct { int64_t tv_sec; int64_t tv_usec; } utimeval_t;

static uint32_t ubuf_size(int v, uint32_t minv, int force) {
    uint32_t val = v < 0 ? 0 : (uint32_t)v;
    if (!force && val > UNIX_BUF_MAX) val = UNIX_BUF_MAX;
    if (val > (64u << 20)) val = 64u << 20;
    val *= 2;
    return val < minv ? minv : val;
}

int64_t unix_op_setopt(vnode_t *vn, int level, int optname, const void *val, uint32_t len) {
    unix_sock_t *s = vn->fs_data;
    if (level != SOL_SOCKET_K) return -EOPNOTSUPP;
    if (optname == SO_RCVTIMEO_K || optname == SO_SNDTIMEO_K) {
        if (len < sizeof(utimeval_t)) return -EINVAL;
        const utimeval_t *tv = val;
        if (tv->tv_usec < 0 || tv->tv_usec >= 1000000) return -EDOM;
        int64_t ms = tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000;
        if (ms < 0) ms = 0;
        if (optname == SO_RCVTIMEO_K) s->rcvtimeo_ms = ms; else s->sndtimeo_ms = ms;
        return 0;
    }
    if (len < sizeof(int)) return -EINVAL;
    int v = *(const int *)val;
    switch (optname) {
    case SO_PASSCRED_K: s->passcred = v != 0; return 0;
    case SO_REUSEADDR_K:
    case SO_KEEPALIVE_K: return 0;
    case SO_SNDBUFFORCE_K:
    case SO_RCVBUFFORCE_K: {
        task_t *me = syscall_cur_task();
        if (me && me->uid != 0) return -EPERM;
        if (optname == SO_SNDBUFFORCE_K) s->sndbuf = ubuf_size(v, UNIX_MIN_SND, 1);
        else                             s->rcvbuf = ubuf_size(v, UNIX_MIN_RCV, 1);
        return 0;
    }
    case SO_SNDBUF_K: s->sndbuf = ubuf_size(v, UNIX_MIN_SND, 0); return 0;
    case SO_RCVBUF_K: s->rcvbuf = ubuf_size(v, UNIX_MIN_RCV, 0); return 0;
    default: return -ENOPROTOOPT;
    }
}

int64_t unix_op_getopt(vnode_t *vn, int level, int optname, void *val, uint32_t *len) {
    unix_sock_t *s = vn->fs_data;
    if (level != SOL_SOCKET_K) return -EOPNOTSUPP;
    if (optname == SO_RCVTIMEO_K || optname == SO_SNDTIMEO_K) {
        if (*len < sizeof(utimeval_t)) return -EINVAL;
        int64_t ms = optname == SO_RCVTIMEO_K ? s->rcvtimeo_ms : s->sndtimeo_ms;
        utimeval_t tv = { ms / 1000, (ms % 1000) * 1000 };
        memcpy(val, &tv, sizeof tv);
        *len = sizeof tv;
        return 0;
    }
    if (optname == SO_PEERCRED_K) {
        struct { uint32_t pid, uid, gid; } cr = { 0, 65534, 65534 };
        if (s->has_peercred) { cr.pid = s->peer_pid; cr.uid = s->peer_uid; cr.gid = s->peer_gid; }
        if (*len < sizeof cr) return -EINVAL;
        memcpy(val, &cr, sizeof cr);
        *len = sizeof cr;
        return 0;
    }
    if (*len < sizeof(int)) return -EINVAL;
    int v;
    switch (optname) {
    case SO_TYPE_K:       v = s->type; break;
    case SO_ERROR_K: {
        uint64_t q = spinlock_acquire_irqsave(&s->lock);
        v = s->err;
        s->err = 0;
        spinlock_release_irqrestore(&s->lock, q);
        break;
    }
    case SO_SNDBUF_K:     v = (int)s->sndbuf; break;
    case SO_RCVBUF_K:     v = (int)s->rcvbuf; break;
    case SO_PASSCRED_K:   v = s->passcred; break;
    case SO_REUSEADDR_K:
    case SO_KEEPALIVE_K:  v = 0; break;
    case SO_ACCEPTCONN_K: v = s->state == U_LISTEN; break;
    case SO_PROTOCOL_K:   v = 0; break;
    case SO_DOMAIN_K:     v = AF_UNIX; break;
    default: return -ENOPROTOOPT;
    }
    memcpy(val, &v, sizeof v);
    *len = sizeof(int);
    return 0;
}
