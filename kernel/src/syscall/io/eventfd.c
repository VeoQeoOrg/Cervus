#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/drivers/timer.h"
#include <string.h>
#include <stdlib.h>

#define EFD_NONBLOCK  0x800
#define EFD_CLOEXEC   0x80000
#define EFD_SEMAPHORE 1

#define TFD_NONBLOCK  0x800
#define TFD_CLOEXEC   0x80000
#define TFD_TIMER_ABSTIME 1

extern int64_t clock_realtime_offset_ns(void);

typedef struct {
    uint64_t   count;
    int        semaphore;
    int        nonblock;
    spinlock_t lock;
    task_t    *waiter;
} eventfd_t;

typedef struct {
    uint64_t   expires_ns;
    uint64_t   interval_ns;
    uint64_t   ticks;
    int        clockid;
    int        nonblock;
    spinlock_t lock;
    task_t    *waiter;
} timerfd_t;

static void wake_one(task_t **slot)
{
    task_t *w = *slot;
    if (!w) return;
    *slot = NULL;
    task_unblock(w);
}

static int64_t efd_read(vnode_t *n, void *buf, size_t len, uint64_t off)
{
    (void)off;
    if (len < sizeof(uint64_t)) return -EINVAL;
    eventfd_t *e = (eventfd_t *)n->fs_data;
    task_t *me = syscall_cur_task();

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&e->lock);
        if (e->count) {
            uint64_t give = e->semaphore ? 1 : e->count;
            e->count -= give;
            spinlock_release_irqrestore(&e->lock, f);
            memcpy(buf, &give, sizeof give);
            return (int64_t)sizeof(uint64_t);
        }
        if (e->nonblock) {
            spinlock_release_irqrestore(&e->lock, f);
            return -EAGAIN;
        }
        e->waiter = me;
        spinlock_release_irqrestore(&e->lock, f);

        if (me) {
            me->runnable = false;
            me->state = TASK_BLOCKED;
            sched_reschedule();
            if (me->pending_kill) return -EINTR;
        } else {
            task_yield();
        }
    }
}

static int64_t efd_write(vnode_t *n, const void *buf, size_t len, uint64_t off)
{
    (void)off;
    if (len < sizeof(uint64_t)) return -EINVAL;
    eventfd_t *e = (eventfd_t *)n->fs_data;

    uint64_t add;
    memcpy(&add, buf, sizeof add);
    if (add == (uint64_t)-1) return -EINVAL;

    uint64_t f = spinlock_acquire_irqsave(&e->lock);
    if (e->count + add < e->count) {
        spinlock_release_irqrestore(&e->lock, f);
        return -EAGAIN;
    }
    e->count += add;
    wake_one(&e->waiter);
    spinlock_release_irqrestore(&e->lock, f);
    return (int64_t)sizeof(uint64_t);
}

static int efd_poll(vnode_t *n, int events)
{
    eventfd_t *e = (eventfd_t *)n->fs_data;
    int out = 0;
    uint64_t f = spinlock_acquire_irqsave(&e->lock);
    if ((events & POLLIN) && e->count) out |= POLLIN;
    if ((events & POLLOUT) && e->count != (uint64_t)-2) out |= POLLOUT;
    spinlock_release_irqrestore(&e->lock, f);
    return out;
}

static int64_t efd_ioctl(vnode_t *n, uint64_t req, void *arg)
{
    eventfd_t *e = (eventfd_t *)n->fs_data;
    if (req == 0x5481) {
        int on = arg ? *(int *)arg : 0;
        e->nonblock = on ? 1 : 0;
        return 0;
    }
    return -ENOTTY;
}

static void efd_unref(vnode_t *n)
{
    if (--n->refcount > 0) return;
    free(n->fs_data);
    free(n);
}

static const vnode_ops_t EFD_OPS = {
    .read  = efd_read,
    .write = efd_write,
    .poll  = efd_poll,
    .ioctl = efd_ioctl,
    .unref = efd_unref,
};

int64_t sys_eventfd(uint64_t initval, uint64_t flags, uint64_t unused)
{
    (void)unused;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    eventfd_t *e = calloc(1, sizeof *e);
    if (!e) return -ENOMEM;
    e->count     = initval;
    e->semaphore = (flags & EFD_SEMAPHORE) ? 1 : 0;
    e->nonblock  = (flags & EFD_NONBLOCK) ? 1 : 0;
    e->lock = (spinlock_t)SPINLOCK_INIT;

    vnode_t *vn = calloc(1, sizeof *vn);
    if (!vn) { free(e); return -ENOMEM; }
    vn->type     = VFS_NODE_CHARDEV;
    vn->ops      = &EFD_OPS;
    vn->fs_data  = e;
    vn->refcount = 1;

    vfs_file_t *file = vfs_file_alloc();
    if (!file) { free(vn); free(e); return -ENOMEM; }
    file->vnode = vn;
    file->flags = 2;

    int fd = fd_alloc(t->fd_table, file, 0);
    if (fd < 0) { vfs_file_free(file); return -EMFILE; }
    if (flags & EFD_CLOEXEC) fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    return fd;
}

static int64_t tfd_read(vnode_t *n, void *buf, size_t len, uint64_t off)
{
    (void)off;
    if (len < sizeof(uint64_t)) return -EINVAL;
    timerfd_t *tm = (timerfd_t *)n->fs_data;
    task_t *me = syscall_cur_task();

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&tm->lock);
        uint64_t now = sched_now_ns();
        if (tm->expires_ns && now >= tm->expires_ns) {
            uint64_t fired = 1;
            if (tm->interval_ns) {
                uint64_t late = now - tm->expires_ns;
                fired += late / tm->interval_ns;
                tm->expires_ns = now + tm->interval_ns - (late % tm->interval_ns);
            } else {
                tm->expires_ns = 0;
            }
            tm->ticks = 0;
            spinlock_release_irqrestore(&tm->lock, f);
            memcpy(buf, &fired, sizeof fired);
            return (int64_t)sizeof(uint64_t);
        }
        if (tm->nonblock || !tm->expires_ns) {
            spinlock_release_irqrestore(&tm->lock, f);
            return -EAGAIN;
        }
        uint64_t until = tm->expires_ns;
        spinlock_release_irqrestore(&tm->lock, f);

        if (me) {
            uint64_t now2 = sched_now_ns();
            task_sleep_ns(until > now2 ? until - now2 : 0);
            if (me->pending_kill) return -EINTR;
        } else {
            task_yield();
        }
    }
}

static int tfd_poll(vnode_t *n, int events)
{
    timerfd_t *tm = (timerfd_t *)n->fs_data;
    int out = 0;
    uint64_t f = spinlock_acquire_irqsave(&tm->lock);
    if ((events & POLLIN) && tm->expires_ns && sched_now_ns() >= tm->expires_ns)
        out |= POLLIN;
    spinlock_release_irqrestore(&tm->lock, f);
    return out;
}

static int64_t tfd_ioctl(vnode_t *n, uint64_t req, void *arg)
{
    timerfd_t *tm = (timerfd_t *)n->fs_data;
    if (req == 0x5481) {
        int on = arg ? *(int *)arg : 0;
        tm->nonblock = on ? 1 : 0;
        return 0;
    }
    return -ENOTTY;
}

static void tfd_unref(vnode_t *n)
{
    if (--n->refcount > 0) return;
    free(n->fs_data);
    free(n);
}

static const vnode_ops_t TFD_OPS = {
    .read  = tfd_read,
    .poll  = tfd_poll,
    .ioctl = tfd_ioctl,
    .unref = tfd_unref,
};

int64_t sys_timerfd_create(uint64_t clockid, uint64_t flags, uint64_t unused)
{
    (void)unused;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    timerfd_t *tm = calloc(1, sizeof *tm);
    if (!tm) return -ENOMEM;
    tm->clockid  = (int)clockid;
    tm->nonblock = (flags & TFD_NONBLOCK) ? 1 : 0;
    tm->lock = (spinlock_t)SPINLOCK_INIT;

    vnode_t *vn = calloc(1, sizeof *vn);
    if (!vn) { free(tm); return -ENOMEM; }
    vn->type     = VFS_NODE_CHARDEV;
    vn->ops      = &TFD_OPS;
    vn->fs_data  = tm;
    vn->refcount = 1;

    vfs_file_t *file = vfs_file_alloc();
    if (!file) { free(vn); free(tm); return -ENOMEM; }
    file->vnode = vn;
    file->flags = 0;

    int fd = fd_alloc(t->fd_table, file, 0);
    if (fd < 0) { vfs_file_free(file); return -EMFILE; }
    if (flags & TFD_CLOEXEC) fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    return fd;
}

static uint64_t timerfd_deadline(timerfd_t *tm, uint64_t flags, uint64_t value_ns)
{
    if (!value_ns) return 0;
    if (!(flags & TFD_TIMER_ABSTIME)) return sched_now_ns() + value_ns;
    int64_t at = (int64_t)value_ns;
    if (tm->clockid == 0 || tm->clockid == 5) at -= clock_realtime_offset_ns();
    return at > 0 ? (uint64_t)at : 1;
}

static void timerfd_current(timerfd_t *tm, uint64_t out[4])
{
    uint64_t now = sched_now_ns();
    uint64_t left = (tm->expires_ns > now) ? tm->expires_ns - now : 0;
    out[0] = tm->interval_ns / 1000000000ULL;
    out[1] = tm->interval_ns % 1000000000ULL;
    out[2] = left / 1000000000ULL;
    out[3] = left % 1000000000ULL;
}

static int64_t timerfd_settime_on(timerfd_t *tm, uint64_t flags, uint64_t value_ptr, uint64_t old_ptr)
{
    uint64_t spec[4];
    if (syscall_copy_from_user(spec, (void *)value_ptr, sizeof spec) < 0) return -EFAULT;

    uint64_t interval = spec[0] * 1000000000ULL + spec[1];
    uint64_t initial  = spec[2] * 1000000000ULL + spec[3];

    uint64_t f = spinlock_acquire_irqsave(&tm->lock);
    if (old_ptr) {
        uint64_t out[4];
        timerfd_current(tm, out);
        spinlock_release_irqrestore(&tm->lock, f);
        if (syscall_copy_to_user((void *)old_ptr, out, sizeof out) < 0) return -EFAULT;
        f = spinlock_acquire_irqsave(&tm->lock);
    }
    tm->interval_ns = interval;
    tm->expires_ns  = timerfd_deadline(tm, flags, initial);
    wake_one(&tm->waiter);
    spinlock_release_irqrestore(&tm->lock, f);
    return 0;
}

int64_t sys_timerfd_settime2(uint64_t fd, uint64_t flags, uint64_t value_ptr, uint64_t old_ptr)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    vfs_file_t *file = fd_get(t->fd_table, (int)fd);
    if (!file) return -EBADF;
    int64_t r = -EINVAL;
    if (file->vnode && file->vnode->ops == &TFD_OPS)
        r = timerfd_settime_on((timerfd_t *)file->vnode->fs_data, flags, value_ptr, old_ptr);
    fd_put(file);
    return r;
}

int64_t sys_timerfd_settime(uint64_t fd, uint64_t value_ptr, uint64_t old_ptr)
{
    return sys_timerfd_settime2(fd, 0, value_ptr, old_ptr);
}

int64_t sys_timerfd_gettime(uint64_t fd, uint64_t cur_ptr)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;
    if (!cur_ptr) return -EFAULT;

    vfs_file_t *file = fd_get(t->fd_table, (int)fd);
    if (!file) return -EBADF;
    int64_t r = -EINVAL;
    if (file->vnode && file->vnode->ops == &TFD_OPS) {
        timerfd_t *tm = (timerfd_t *)file->vnode->fs_data;
        uint64_t out[4];
        uint64_t f = spinlock_acquire_irqsave(&tm->lock);
        timerfd_current(tm, out);
        spinlock_release_irqrestore(&tm->lock, f);
        r = syscall_copy_to_user((void *)cur_ptr, out, sizeof out) < 0 ? -EFAULT : 0;
    }
    fd_put(file);
    return r;
}
