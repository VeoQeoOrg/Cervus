#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/drivers/timer.h"
#include <string.h>
#include <stdlib.h>

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLL_MAX_WATCHED 64

typedef struct {
    uint32_t events;
    uint64_t data;
} __attribute__((packed)) epoll_event_t;

typedef struct {
    int      used;
    int      fd;
    uint32_t events;
    uint64_t data;
} epoll_watch_t;

typedef struct {
    epoll_watch_t watched[EPOLL_MAX_WATCHED];
    spinlock_t    lock;
} epoll_set_t;

extern int vfs_poll_file(vfs_file_t *file, int events);

static void epoll_unref(vnode_t *n)
{
    if (--n->refcount > 0) return;
    free(n->fs_data);
    free(n);
}

static int epoll_poll(vnode_t *n, int events)
{
    (void)n; (void)events;
    return 0;
}

static const vnode_ops_t EPOLL_OPS = {
    .poll  = epoll_poll,
    .unref = epoll_unref,
};

static epoll_set_t *epoll_from_fd(task_t *t, int fd)
{
    vfs_file_t *file = fd_get(t->fd_table, fd);
    if (!file || !file->vnode || file->vnode->ops != &EPOLL_OPS) return NULL;
    return (epoll_set_t *)file->vnode->fs_data;
}

int64_t sys_epoll_create(uint64_t flags, uint64_t a, uint64_t b)
{
    (void)flags; (void)a; (void)b;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    epoll_set_t *ep = calloc(1, sizeof *ep);
    if (!ep) return -ENOMEM;
    ep->lock = (spinlock_t)SPINLOCK_INIT;

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
    return fd;
}

int64_t sys_epoll_ctl(uint64_t epfd, uint64_t op, uint64_t fd_and_event)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    struct { int fd; uint64_t evptr; } args;
    if (syscall_copy_from_user(&args, (void *)fd_and_event, sizeof args) < 0)
        return -EFAULT;

    epoll_set_t *ep = epoll_from_fd(t, (int)epfd);
    if (!ep) return -EBADF;

    epoll_event_t ev = { 0, 0 };
    if (op != EPOLL_CTL_DEL) {
        if (!args.evptr) return -EFAULT;
        if (syscall_copy_from_user(&ev, (void *)args.evptr, sizeof ev) < 0)
            return -EFAULT;
    }

    vfs_file_t *target = fd_get(t->fd_table, args.fd);
    if (target) fd_put(target);
    else if (op != EPOLL_CTL_DEL) return -EBADF;

    uint64_t f = spinlock_acquire_irqsave(&ep->lock);
    int64_t rc = 0;

    int slot = -1, free_slot = -1;
    for (int i = 0; i < EPOLL_MAX_WATCHED; i++) {
        if (ep->watched[i].used && ep->watched[i].fd == args.fd) { slot = i; break; }
        if (!ep->watched[i].used && free_slot < 0) free_slot = i;
    }

    switch (op) {
        case EPOLL_CTL_ADD:
            if (slot >= 0) { rc = -EEXIST; break; }
            if (free_slot < 0) { rc = -ENOSPC; break; }
            ep->watched[free_slot].used   = 1;
            ep->watched[free_slot].fd     = args.fd;
            ep->watched[free_slot].events = ev.events;
            ep->watched[free_slot].data   = ev.data;
            break;
        case EPOLL_CTL_MOD:
            if (slot < 0) { rc = -ENOENT; break; }
            ep->watched[slot].events = ev.events;
            ep->watched[slot].data   = ev.data;
            break;
        case EPOLL_CTL_DEL:
            if (slot < 0) { rc = -ENOENT; break; }
            ep->watched[slot].used = 0;
            break;
        default:
            rc = -EINVAL;
    }

    spinlock_release_irqrestore(&ep->lock, f);
    return rc;
}

int64_t sys_epoll_wait(uint64_t epfd, uint64_t events_ptr, uint64_t max_and_timeout)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    struct { int maxevents; int timeout_ms; } args;
    if (syscall_copy_from_user(&args, (void *)max_and_timeout, sizeof args) < 0)
        return -EFAULT;
    if (args.maxevents <= 0) return -EINVAL;
    if (args.maxevents > EPOLL_MAX_WATCHED) args.maxevents = EPOLL_MAX_WATCHED;

    epoll_set_t *ep = epoll_from_fd(t, (int)epfd);
    if (!ep) return -EBADF;

    uint64_t deadline = 0;
    if (args.timeout_ms > 0)
        deadline = sched_now_ns() + (uint64_t)args.timeout_ms * 1000000ULL;

    epoll_event_t out[EPOLL_MAX_WATCHED];

    for (;;) {
        int n = 0;

        uint64_t f = spinlock_acquire_irqsave(&ep->lock);
        epoll_watch_t snapshot[EPOLL_MAX_WATCHED];
        memcpy(snapshot, ep->watched, sizeof snapshot);
        spinlock_release_irqrestore(&ep->lock, f);

        for (int i = 0; i < EPOLL_MAX_WATCHED && n < args.maxevents; i++) {
            if (!snapshot[i].used) continue;
            vfs_file_t *file = fd_get(t->fd_table, snapshot[i].fd);
            if (!file) {
                out[n].events = POLLERR;
                out[n].data   = snapshot[i].data;
                n++;
                continue;
            }
            int want = (int)(snapshot[i].events & (POLLIN | POLLOUT | POLLPRI));
            if (!want) want = POLLIN;
            int got  = vfs_poll_file(file, want);
            fd_put(file);
            got &= (want | POLLERR | POLLHUP | POLLNVAL);
            if (got > 0) {
                out[n].events = (uint32_t)got;
                out[n].data   = snapshot[i].data;
                n++;
            }
        }

        if (n > 0) {
            if (syscall_copy_to_user((void *)events_ptr, out,
                                     (size_t)n * sizeof(epoll_event_t)) < 0)
                return -EFAULT;
            return n;
        }

        if (args.timeout_ms == 0) return 0;
        if (deadline && sched_now_ns() >= deadline) return 0;

        task_t *me = syscall_cur_task();
        if (me && me->pending_kill) return -EINTR;
        task_sleep_ns(1000000ULL);
    }
}
