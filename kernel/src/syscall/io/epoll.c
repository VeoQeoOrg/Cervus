#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/kmutex.h"
#include "../../../include/sched/sched.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/fs/pollwait.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/signal/signal.h"
#include <string.h>
#include <stdlib.h>

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLONESHOT_K (1u << 30)
#define EPOLL_MAX_WATCHED 65536
#define EPOLL_MAX_EVENTS  4096

typedef struct {
    uint32_t events;
    uint64_t data;
} __attribute__((packed)) epoll_event_t;

typedef struct {
    int         fd;
    uint32_t    events;
    uint64_t    data;
    vfs_file_t *file;
    int         disabled;
} epoll_watch_t;

typedef struct {
    epoll_watch_t *w;
    int            n;
    int            cap;
    kmutex_t       lock;
} epoll_set_t;

extern int vfs_poll_file(vfs_file_t *file, int events);

static void epoll_unref(vnode_t *n)
{
    if (--n->refcount > 0) return;
    epoll_set_t *ep = (epoll_set_t *)n->fs_data;
    if (ep) free(ep->w);
    free(ep);
    free(n);
}

static int watch_want(const epoll_watch_t *w)
{
    int want = (int)(w->events & (POLLIN | POLLOUT | POLLPRI | POLLRDHUP | POLLRDNORM | POLLWRNORM));
    return want ? want : POLLIN;
}

static void drop_watch(epoll_set_t *ep, int i)
{
    ep->w[i] = ep->w[ep->n - 1];
    ep->n--;
}

static int scan(task_t *t, epoll_set_t *ep, epoll_event_t *out, int maxevents, int consume)
{
    int n = 0;
    for (int i = 0; i < ep->n && n < maxevents; ) {
        epoll_watch_t *w = &ep->w[i];
        vfs_file_t *file = fd_get(t->fd_table, w->fd);
        if (!file || file != w->file) {
            if (file) fd_put(file);
            drop_watch(ep, i);
            continue;
        }
        if (w->disabled) { fd_put(file); i++; continue; }
        int want = watch_want(w);
        int got = poll_expand(vfs_poll_file(file, want));
        fd_put(file);
        got &= (want | POLLERR | POLLHUP | POLLNVAL);
        if (got) {
            if (out) {
                out[n].events = (uint32_t)got;
                out[n].data   = w->data;
            }
            n++;
            if (consume && (w->events & EPOLLONESHOT_K)) w->disabled = 1;
        }
        i++;
    }
    return n;
}

static int epoll_poll(vnode_t *vn, int events)
{
    epoll_set_t *ep = (epoll_set_t *)vn->fs_data;
    task_t *t = syscall_cur_task();
    if (!ep || !t || !t->fd_table || !(events & POLLIN)) return 0;
    if (!kmutex_trylock(&ep->lock)) return 0;
    int ready = scan(t, ep, NULL, 1, 0);
    kmutex_unlock(&ep->lock);
    return ready ? POLLIN : 0;
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

int64_t sys_epoll_ctl(uint64_t epfd, uint64_t op, uint64_t fd_and_event)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    struct { int fd; uint64_t evptr; } args;
    if (syscall_copy_from_user(&args, (void *)fd_and_event, sizeof args) < 0)
        return -EFAULT;

    epoll_event_t ev = { 0, 0 };
    if (op != EPOLL_CTL_DEL) {
        if (!args.evptr) return -EFAULT;
        if (syscall_copy_from_user(&ev, (void *)args.evptr, sizeof ev) < 0)
            return -EFAULT;
    }

    if ((int)epfd == args.fd) return -EINVAL;
    vfs_file_t *target = fd_get(t->fd_table, args.fd);
    if (!target) return -EBADF;

    vfs_file_t *epf = epoll_file(t, (int)epfd);
    if (!epf) { fd_put(target); return -EBADF; }
    epoll_set_t *ep = (epoll_set_t *)epf->vnode->fs_data;

    kmutex_lock(&ep->lock);
    int64_t rc = 0;
    int slot = -1;
    for (int i = 0; i < ep->n; i++) {
        if (ep->w[i].fd != args.fd) continue;
        if (ep->w[i].file != target) { drop_watch(ep, i); break; }
        slot = i;
        break;
    }

    switch (op) {
        case EPOLL_CTL_ADD:
            if (slot >= 0) { rc = -EEXIST; break; }
            if (ep->n >= EPOLL_MAX_WATCHED) { rc = -ENOSPC; break; }
            if (ep->n == ep->cap) {
                int ncap = ep->cap ? ep->cap * 2 : 16;
                epoll_watch_t *nw = realloc(ep->w, (size_t)ncap * sizeof *nw);
                if (!nw) { rc = -ENOMEM; break; }
                ep->w = nw;
                ep->cap = ncap;
            }
            ep->w[ep->n].fd       = args.fd;
            ep->w[ep->n].events   = ev.events;
            ep->w[ep->n].data     = ev.data;
            ep->w[ep->n].file     = target;
            ep->w[ep->n].disabled = 0;
            ep->n++;
            break;
        case EPOLL_CTL_MOD:
            if (slot < 0) { rc = -ENOENT; break; }
            ep->w[slot].events   = ev.events;
            ep->w[slot].data     = ev.data;
            ep->w[slot].disabled = 0;
            break;
        case EPOLL_CTL_DEL:
            if (slot < 0) { rc = -ENOENT; break; }
            drop_watch(ep, slot);
            break;
        default:
            rc = -EINVAL;
    }

    kmutex_unlock(&ep->lock);
    fd_put(epf);
    fd_put(target);
    return rc;
}

static int64_t epoll_wait_on(task_t *t, epoll_set_t *ep, uint64_t events_ptr, int maxevents, int timeout_ms)
{
    uint64_t deadline = 0;
    if (timeout_ms > 0)
        deadline = sched_now_ns() + (uint64_t)timeout_ms * 1000000ULL;

    epoll_event_t *out = malloc((size_t)maxevents * sizeof *out);
    if (!out) return -ENOMEM;

    for (;;) {
        uint64_t seq = poll_seq();
        kmutex_lock(&ep->lock);
        int n = scan(t, ep, out, maxevents, 1);
        kmutex_unlock(&ep->lock);

        if (n > 0) {
            int64_t r = n;
            if (syscall_copy_to_user((void *)events_ptr, out, (size_t)n * sizeof *out) < 0) r = -EFAULT;
            free(out);
            return r;
        }

        if (timeout_ms == 0) break;
        if (deadline && sched_now_ns() >= deadline) break;

        task_t *me = syscall_cur_task();
        if (me && (me->pending_kill || signal_pending_deliverable(me))) { free(out); return -EINTR; }
        uint64_t wake = sched_now_ns() + 20000000ULL;
        if (deadline && deadline < wake) wake = deadline;
        poll_wait_since(seq, wake);
    }
    free(out);
    return 0;
}

int64_t sys_epoll_wait(uint64_t epfd, uint64_t events_ptr, uint64_t max_and_timeout)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    struct { int maxevents; int timeout_ms; } args;
    if (syscall_copy_from_user(&args, (void *)max_and_timeout, sizeof args) < 0)
        return -EFAULT;
    if (args.maxevents <= 0) return -EINVAL;
    if (args.maxevents > EPOLL_MAX_EVENTS) args.maxevents = EPOLL_MAX_EVENTS;
    if (!syscall_uptr_validate_write((void *)events_ptr, (size_t)args.maxevents * sizeof(epoll_event_t)))
        return -EFAULT;

    vfs_file_t *epf = epoll_file(t, (int)epfd);
    if (!epf) return -EBADF;
    int64_t r = epoll_wait_on(t, (epoll_set_t *)epf->vnode->fs_data, events_ptr, args.maxevents, args.timeout_ms);
    fd_put(epf);
    return r;
}
