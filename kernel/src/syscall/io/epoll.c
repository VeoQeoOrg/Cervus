#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/kmutex.h"
#include "../../../include/sched/sched.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/fs/pollwait.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/signal/signal.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLONESHOT_K (1u << 30)
#define EPOLLET_K      (1u << 31)
#define EPOLL_MAX_WATCHED 65536
#define EPOLL_MAX_EVENTS  4096
#define EP_QMAX 4

typedef struct {
    uint32_t events;
    uint64_t data;
} __attribute__((packed)) epoll_event_t;

typedef struct epoll_set epoll_set_t;

typedef struct epitem {
    epoll_set_t   *ep;
    vfs_file_t    *file;
    int            fd;
    uint32_t       events;
    uint64_t       data;
    int            disabled;
    int            legacy;
    uint64_t       deadline;
    int            on_rd;
    int            on_poll;
    uint64_t       seen;
    struct epitem *rd_next;
    struct epitem *f_next;
    int            nq;
    waitq_t       *q[EP_QMAX];
    void         (*put[EP_QMAX])(void *);
    void          *obj[EP_QMAX];
    waitq_cb_t     cb;
} epitem_t;

struct epoll_set {
    epitem_t  **items;
    int         n;
    int         cap;
    epitem_t  **tmp;
    epitem_t  **polled;
    int         npolled;
    uint64_t    gen;
    kmutex_t    lock;
    spinlock_t  rdlock;
    epitem_t   *rd_head;
    epitem_t   *rd_tail;
    waitq_t     wq;
};

static kmutex_t g_ep_mutex = KMUTEX_INIT;

extern int vfs_poll_file(vfs_file_t *file, int events);

static const vnode_ops_t EPOLL_OPS;

static int watch_want(const epitem_t *it)
{
    int want = (int)(it->events & (POLLIN | POLLOUT | POLLPRI | POLLRDHUP | POLLRDNORM | POLLWRNORM));
    return want ? want : POLLIN;
}

static void ep_queue(epoll_set_t *ep, epitem_t *it)
{
    uint64_t f = spinlock_acquire_irqsave(&ep->rdlock);
    if (!it->on_rd) {
        it->on_rd = 1;
        it->rd_next = NULL;
        if (ep->rd_tail) ep->rd_tail->rd_next = it;
        else ep->rd_head = it;
        ep->rd_tail = it;
    }
    spinlock_release_irqrestore(&ep->rdlock, f);
}

static void ep_item_cb(waitq_cb_t *cb)
{
    epitem_t *it = (epitem_t *)((char *)cb - offsetof(epitem_t, cb));
    epoll_set_t *ep = it->ep;
    ep_queue(ep, it);
    waitq_wake_pollers(&ep->wq);
}

static void ep_qproc(poll_table_t *pt, waitq_t *q, void (*put)(void *), void *obj)
{
    epitem_t *it = (epitem_t *)pt->qctx;
    for (int i = 0; i < it->nq; i++) {
        if (it->q[i] != q) continue;
        if (put) put(obj);
        return;
    }
    if (it->nq >= EP_QMAX || !waitq_cb_add(q, &it->cb)) {
        it->legacy = 1;
        if (put) put(obj);
        return;
    }
    it->q[it->nq] = q;
    it->put[it->nq] = put;
    it->obj[it->nq] = obj;
    it->nq++;
}

static int ep_poll_item(epitem_t *it, int reg)
{
    task_t *me = syscall_cur_task();
    poll_table_t pt;
    memset(&pt, 0, sizeof pt);
    if (reg) {
        pt.qproc = ep_qproc;
        pt.qctx = it;
    } else {
        pt.noreg = 1;
    }
    pt.cur_file = it->file;
    void *saved = me ? me->poll_tbl : NULL;
    if (me) me->poll_tbl = &pt;
    int want = watch_want(it);
    int got = poll_expand(vfs_poll_file(it->file, want)) & (want | POLLERR | POLLHUP | POLLNVAL);
    if (me) me->poll_tbl = saved;
    if ((reg && !pt.touched) || pt.legacy) it->legacy = 1;
    it->deadline = pt.deadline_ns;
    return got;
}

static void ep_note_polled(epoll_set_t *ep, epitem_t *it)
{
    if (it->on_poll || it->disabled || (!it->legacy && !it->deadline)) return;
    it->on_poll = 1;
    ep->polled[ep->npolled++] = it;
}

static void ep_unlink_rd(epoll_set_t *ep, epitem_t *it)
{
    uint64_t f = spinlock_acquire_irqsave(&ep->rdlock);
    if (it->on_rd) {
        epitem_t *prev = NULL;
        for (epitem_t *x = ep->rd_head; x; prev = x, x = x->rd_next) {
            if (x != it) continue;
            if (prev) prev->rd_next = x->rd_next;
            else ep->rd_head = x->rd_next;
            if (ep->rd_tail == x) ep->rd_tail = prev;
            break;
        }
        it->on_rd = 0;
    }
    spinlock_release_irqrestore(&ep->rdlock, f);
}

static void ep_remove(epoll_set_t *ep, epitem_t *it)
{
    for (int i = 0; i < it->nq; i++) {
        waitq_cb_del(it->q[i], &it->cb);
        if (it->put[i]) it->put[i](it->obj[i]);
    }
    it->nq = 0;
    ep_unlink_rd(ep, it);
    if (it->on_poll) {
        for (int i = 0; i < ep->npolled; i++) {
            if (ep->polled[i] != it) continue;
            ep->polled[i] = ep->polled[--ep->npolled];
            break;
        }
    }
    epitem_t **pp = (epitem_t **)&it->file->ep_items;
    while (*pp && *pp != it) pp = &(*pp)->f_next;
    if (*pp) *pp = it->f_next;
    for (int i = 0; i < ep->n; i++) {
        if (ep->items[i] != it) continue;
        ep->items[i] = ep->items[--ep->n];
        break;
    }
    free(it);
}

static int ep_collect(epoll_set_t *ep, epoll_event_t *out, int maxevents, int consume,
                      uint64_t *dl_out, int *legacy_out)
{
    uint64_t gen = ++ep->gen;
    int nrd = 0;
    uint64_t f = spinlock_acquire_irqsave(&ep->rdlock);
    for (epitem_t *x = ep->rd_head, *nx; x; x = nx) {
        nx = x->rd_next;
        x->rd_next = NULL;
        x->on_rd = 0;
        ep->tmp[nrd++] = x;
    }
    ep->rd_head = ep->rd_tail = NULL;
    spinlock_release_irqrestore(&ep->rdlock, f);

    int n = 0;
    uint64_t dl = 0;
    int legacy = 0;
    int npolled = ep->npolled;
    for (int pass = 0; pass < 2; pass++) {
        int cnt = pass ? npolled : nrd;
        for (int i = 0; i < cnt; i++) {
            epitem_t *it = pass ? ep->polled[i] : ep->tmp[i];
            if (it->seen == gen || it->disabled) continue;
            if (n >= maxevents) {
                ep_queue(ep, it);
                continue;
            }
            it->seen = gen;
            int got = ep_poll_item(it, 0);
            if (got) {
                if (out) {
                    out[n].events = (uint32_t)got;
                    out[n].data   = it->data;
                }
                n++;
                if (consume && (it->events & EPOLLONESHOT_K)) it->disabled = 1;
                else if (!consume || !(it->events & EPOLLET_K)) ep_queue(ep, it);
            } else {
                if (it->deadline && (!dl || it->deadline < dl)) dl = it->deadline;
                if (it->legacy) legacy = 1;
            }
        }
    }

    int k = 0;
    for (int i = 0; i < ep->npolled; i++) {
        epitem_t *it = ep->polled[i];
        if (!it->disabled && (it->legacy || it->deadline)) ep->polled[k++] = it;
        else it->on_poll = 0;
    }
    ep->npolled = k;
    for (int i = 0; i < nrd; i++) ep_note_polled(ep, ep->tmp[i]);

    if (dl_out) *dl_out = dl;
    if (legacy_out) *legacy_out = legacy;
    return n;
}

void epoll_file_release(vfs_file_t *file)
{
    kmutex_lock(&g_ep_mutex);
    epitem_t *it;
    while ((it = (epitem_t *)file->ep_items) != NULL) {
        epoll_set_t *ep = it->ep;
        kmutex_lock(&ep->lock);
        ep_remove(ep, it);
        kmutex_unlock(&ep->lock);
    }
    kmutex_unlock(&g_ep_mutex);
}

void epk_destroy(epoll_set_t *ep);
int epk_poll(epoll_set_t *ep, int events);

static void epoll_unref(vnode_t *n)
{
    if (--n->refcount > 0) return;
    epk_destroy((epoll_set_t *)n->fs_data);
    free(n);
}

static int epoll_poll(vnode_t *vn, int events)
{
    return epk_poll((epoll_set_t *)vn->fs_data, events);
}

static const vnode_ops_t EPOLL_OPS = {
    .poll  = epoll_poll,
    .unref = epoll_unref,
};

static vfs_file_t *epoll_file(task_t *t, int fd)
{
    vfs_file_t *file = fd_get(t->fd_table, fd);
    if (!file) return NULL;
    if (!file->vnode || file->vnode->ops != &EPOLL_OPS) { fd_put(file); return NULL; }
    return file;
}

static int ep_has_nested(epoll_set_t *ep)
{
    for (int i = 0; i < ep->n; i++)
        if (ep->items[i]->file->vnode && ep->items[i]->file->vnode->ops == &EPOLL_OPS) return 1;
    return 0;
}

int64_t sys_epoll_create(uint64_t flags, uint64_t a, uint64_t b)
{
    (void)a; (void)b;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    epoll_set_t *ep = calloc(1, sizeof *ep);
    if (!ep) return -ENOMEM;
    kmutex_init(&ep->lock);

    vnode_t *vn = calloc(1, sizeof *vn);
    if (!vn) { free(ep); return -ENOMEM; }
    vn->type     = VFS_NODE_CHARDEV;
    vn->ops      = &EPOLL_OPS;
    vn->fs_data  = ep;
    vn->refcount = 1;

    vfs_file_t *file = vfs_file_alloc();
    if (!file) { free(vn); free(ep); return -ENOMEM; }
    file->vnode = vn;

    int fd = fd_alloc(t->fd_table, file, 0);
    if (fd < 0) { vfs_file_free(file); return -EMFILE; }
    if (flags & 0x80000) fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    return fd;
}

static int ep_grow(epoll_set_t *ep)
{
    if (ep->n < ep->cap) return 0;
    int ncap = ep->cap ? ep->cap * 2 : 16;
    epitem_t **ni = realloc(ep->items, (size_t)ncap * sizeof *ni);
    if (!ni) return -ENOMEM;
    ep->items = ni;
    epitem_t **nt = realloc(ep->tmp, (size_t)ncap * sizeof *nt);
    if (!nt) return -ENOMEM;
    ep->tmp = nt;
    epitem_t **np = realloc(ep->polled, (size_t)ncap * sizeof *np);
    if (!np) return -ENOMEM;
    ep->polled = np;
    ep->cap = ncap;
    return 0;
}

int64_t epk_ctl(epoll_set_t *ep, vfs_file_t *epf, int op, vfs_file_t *target, int fd, uint32_t events, uint64_t data)
{
    int nested = target->vnode && target->vnode->ops == &EPOLL_OPS;
    kmutex_lock(&g_ep_mutex);
    kmutex_lock(&ep->lock);
    int64_t rc = 0;
    epitem_t *it = NULL;
    for (int i = 0; i < ep->n; i++) {
        if (ep->items[i]->fd == fd && ep->items[i]->file == target) { it = ep->items[i]; break; }
    }

    switch (op) {
        case EPOLL_CTL_ADD:
            if (it) { rc = -EEXIST; break; }
            if (ep->n >= EPOLL_MAX_WATCHED) { rc = -ENOSPC; break; }
            if (nested && ((epf && epf->ep_items) || ep_has_nested((epoll_set_t *)target->vnode->fs_data))) {
                rc = -ELOOP;
                break;
            }
            if (ep_grow(ep) < 0) { rc = -ENOMEM; break; }
            it = calloc(1, sizeof *it);
            if (!it) { rc = -ENOMEM; break; }
            it->ep     = ep;
            it->file   = target;
            it->fd     = fd;
            it->events = events;
            it->data   = data;
            it->cb.fn  = ep_item_cb;
            ep->items[ep->n++] = it;
            it->f_next = (epitem_t *)target->ep_items;
            __atomic_store_n(&target->ep_items, it, __ATOMIC_RELEASE);
            if (ep_poll_item(it, 1)) ep_queue(ep, it);
            ep_note_polled(ep, it);
            break;
        case EPOLL_CTL_MOD:
            if (!it) { rc = -ENOENT; break; }
            it->events   = events;
            it->data     = data;
            it->disabled = 0;
            ep_queue(ep, it);
            break;
        case EPOLL_CTL_DEL:
            if (!it) { rc = -ENOENT; break; }
            ep_remove(ep, it);
            break;
        default:
            rc = -EINVAL;
    }

    kmutex_unlock(&ep->lock);
    kmutex_unlock(&g_ep_mutex);
    if (rc == 0 && op != EPOLL_CTL_DEL) waitq_wake_pollers(&ep->wq);
    return rc;
}

int64_t epoll_ctl_args(uint64_t epfd, uint64_t op, int fd, uint64_t evptr)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    epoll_event_t ev = { 0, 0 };
    if (op != EPOLL_CTL_DEL) {
        if (!evptr) return -EFAULT;
        if (syscall_copy_from_user(&ev, (void *)evptr, sizeof ev) < 0)
            return -EFAULT;
    }

    if ((int)epfd == fd) return -EINVAL;
    vfs_file_t *target = fd_get(t->fd_table, fd);
    if (!target) return -EBADF;

    vfs_file_t *epf = epoll_file(t, (int)epfd);
    if (!epf) { fd_put(target); return -EBADF; }
    int64_t rc = epk_ctl((epoll_set_t *)epf->vnode->fs_data, epf, (int)op, target, fd, ev.events, ev.data);
    fd_put(epf);
    fd_put(target);
    return rc;
}

int64_t sys_epoll_ctl(uint64_t epfd, uint64_t op, uint64_t fd_and_event)
{
    struct { int fd; uint64_t evptr; } args;
    if (syscall_copy_from_user(&args, (void *)fd_and_event, sizeof args) < 0)
        return -EFAULT;
    return epoll_ctl_args(epfd, op, args.fd, args.evptr);
}

int64_t epk_wait(epoll_set_t *ep, void *kout, int maxevents, int64_t timeout_ns,
                 uint64_t extra_deadline, volatile int *kick)
{
    epoll_event_t *out = kout;
    uint64_t deadline = 0;
    if (timeout_ns > 0)
        deadline = sched_now_ns() + (uint64_t)timeout_ns;
    uint64_t wake = deadline;
    if (extra_deadline && (!wake || extra_deadline < wake)) wake = extra_deadline;

    poll_table_t pt;
    poll_table_init(&pt);
    int64_t r = 0;
    for (;;) {
        uint64_t gseq = poll_seq();
        uint64_t wseq = poll_table_begin(&pt);
        poll_wait(&ep->wq);
        poll_table_end(&pt);

        uint64_t dl = 0;
        int legacy = 0;
        kmutex_lock(&ep->lock);
        int n = ep_collect(ep, out, maxevents, 1, &dl, &legacy);
        kmutex_unlock(&ep->lock);

        if (n > 0) { r = n; break; }
        if (kick && __atomic_load_n(kick, __ATOMIC_ACQUIRE)) break;
        if (timeout_ns == 0) break;
        uint64_t now = sched_now_ns();
        if (wake && now >= wake) break;

        task_t *me = syscall_cur_task();
        if (me && (me->pending_kill || signal_pending_deliverable(me))) { r = -EINTR; break; }
        pt.legacy = legacy;
        pt.deadline_ns = dl;
        poll_table_block(&pt, gseq, wseq, wake);
        poll_table_release(&pt);
    }
    poll_table_release(&pt);
    return r;
}

static int64_t epoll_wait_on(task_t *t, epoll_set_t *ep, uint64_t events_ptr, int maxevents, int timeout_ms)
{
    (void)t;
    epoll_event_t *out = malloc((size_t)maxevents * sizeof *out);
    if (!out) return -ENOMEM;
    int64_t timeout_ns = timeout_ms < 0 ? -1 : (int64_t)timeout_ms * 1000000LL;
    int64_t r = epk_wait(ep, out, maxevents, timeout_ns, 0, NULL);
    if (r > 0 && syscall_copy_to_user((void *)events_ptr, out, (size_t)r * sizeof *out) < 0) r = -EFAULT;
    free(out);
    return r;
}

epoll_set_t *epk_create(void)
{
    epoll_set_t *ep = calloc(1, sizeof *ep);
    if (ep) kmutex_init(&ep->lock);
    return ep;
}

void epk_destroy(epoll_set_t *ep)
{
    if (!ep) return;
    kmutex_lock(&g_ep_mutex);
    kmutex_lock(&ep->lock);
    while (ep->n) ep_remove(ep, ep->items[ep->n - 1]);
    kmutex_unlock(&ep->lock);
    kmutex_unlock(&g_ep_mutex);
    free(ep->items);
    free(ep->tmp);
    free(ep->polled);
    free(ep);
}

int epk_poll(epoll_set_t *ep, int events)
{
    if (!ep || !(events & POLLIN)) return 0;
    poll_wait(&ep->wq);
    if (!kmutex_trylock(&ep->lock)) return 0;
    uint64_t dl = 0;
    int legacy = 0;
    int ready = ep_collect(ep, NULL, 1, 0, &dl, &legacy);
    kmutex_unlock(&ep->lock);
    if (!ready) {
        if (dl) poll_deadline(dl);
        if (legacy) poll_mark_legacy();
    }
    return ready ? POLLIN : 0;
}

void epk_wake(epoll_set_t *ep)
{
    waitq_wake_pollers(&ep->wq);
}

int64_t epoll_wait_args(uint64_t epfd, uint64_t events_ptr, int maxevents, int timeout_ms)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;
    if (maxevents <= 0) return -EINVAL;
    if (maxevents > EPOLL_MAX_EVENTS) maxevents = EPOLL_MAX_EVENTS;
    if (!syscall_uptr_validate_write((void *)events_ptr, (size_t)maxevents * sizeof(epoll_event_t)))
        return -EFAULT;

    vfs_file_t *epf = epoll_file(t, (int)epfd);
    if (!epf) return -EBADF;
    int64_t r = epoll_wait_on(t, (epoll_set_t *)epf->vnode->fs_data, events_ptr, maxevents, timeout_ms);
    fd_put(epf);
    return r;
}

int64_t sys_epoll_wait(uint64_t epfd, uint64_t events_ptr, uint64_t max_and_timeout)
{
    struct { int maxevents; int timeout_ms; } args;
    if (syscall_copy_from_user(&args, (void *)max_and_timeout, sizeof args) < 0)
        return -EFAULT;
    return epoll_wait_args(epfd, events_ptr, args.maxevents, args.timeout_ms);
}
