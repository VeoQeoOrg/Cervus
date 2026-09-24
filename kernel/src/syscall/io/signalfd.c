#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/signal/signal.h"
#include <string.h>
#include <stdlib.h>

#define SFD_NONBLOCK 0x800
#define SFD_CLOEXEC  0x80000

#define SFD_QUEUE 32

typedef struct {
    uint64_t   mask;
    uint32_t   queue[SFD_QUEUE];
    int        head, tail, count;
    int        nonblock;
    uint32_t   owner_pid;
    spinlock_t lock;
    task_t    *waiter;
} signalfd_t;

typedef struct {
    uint32_t ssi_signo;
    int32_t  ssi_errno;
    int32_t  ssi_code;
    uint32_t ssi_pid;
    uint32_t ssi_uid;
    int32_t  ssi_fd;
    uint32_t ssi_tid;
    uint32_t ssi_band;
    uint32_t ssi_overrun;
    uint32_t ssi_trapno;
    int32_t  ssi_status;
    int32_t  ssi_int;
    uint64_t ssi_ptr;
    uint64_t ssi_utime;
    uint64_t ssi_stime;
    uint64_t ssi_addr;
    uint16_t ssi_addr_lsb;
    uint8_t  pad[46];
} sfd_siginfo_t;

#define SFD_MAX 32
static signalfd_t *g_sfds[SFD_MAX];
static spinlock_t  g_sfds_lock = SPINLOCK_INIT;

static void sfd_register(signalfd_t *s)
{
    uint64_t f = spinlock_acquire_irqsave(&g_sfds_lock);
    for (int i = 0; i < SFD_MAX; i++)
        if (!g_sfds[i]) { g_sfds[i] = s; break; }
    spinlock_release_irqrestore(&g_sfds_lock, f);
}

static void sfd_unregister(signalfd_t *s)
{
    uint64_t f = spinlock_acquire_irqsave(&g_sfds_lock);
    for (int i = 0; i < SFD_MAX; i++)
        if (g_sfds[i] == s) { g_sfds[i] = NULL; break; }
    spinlock_release_irqrestore(&g_sfds_lock, f);
}

int signalfd_deliver(uint32_t pid, int sig)
{
    if (sig < 1 || sig > 63) return 0;

    int taken = 0;
    uint64_t gf = spinlock_acquire_irqsave(&g_sfds_lock);
    for (int i = 0; i < SFD_MAX; i++) {
        signalfd_t *s = g_sfds[i];
        if (!s || s->owner_pid != pid) continue;
        if (!(s->mask & (1ULL << sig))) continue;

        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        if (s->count < SFD_QUEUE) {
            s->queue[s->tail] = (uint32_t)sig;
            s->tail = (s->tail + 1) % SFD_QUEUE;
            s->count++;
        }
        task_t *w = s->waiter;
        s->waiter = NULL;
        spinlock_release_irqrestore(&s->lock, f);
        if (w) task_unblock(w);
        taken = 1;
    }
    spinlock_release_irqrestore(&g_sfds_lock, gf);
    return taken;
}

static int64_t sfd_read(vnode_t *n, void *buf, size_t len, uint64_t off)
{
    (void)off;
    if (len < sizeof(sfd_siginfo_t)) return -EINVAL;
    signalfd_t *s = (signalfd_t *)n->fs_data;
    task_t *me = syscall_cur_task();

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        if (s->count) {
            size_t want = len / sizeof(sfd_siginfo_t);
            size_t got = 0;
            uint8_t *dst = buf;
            while (got < want && s->count) {
                sfd_siginfo_t si;
                memset(&si, 0, sizeof si);
                si.ssi_signo = s->queue[s->head];
                s->head = (s->head + 1) % SFD_QUEUE;
                s->count--;
                memcpy(dst + got * sizeof si, &si, sizeof si);
                got++;
            }
            spinlock_release_irqrestore(&s->lock, f);
            return (int64_t)(got * sizeof(sfd_siginfo_t));
        }
        if (s->nonblock) {
            spinlock_release_irqrestore(&s->lock, f);
            return -EAGAIN;
        }
        s->waiter = me;
        spinlock_release_irqrestore(&s->lock, f);

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

static int sfd_poll(vnode_t *n, int events)
{
    signalfd_t *s = (signalfd_t *)n->fs_data;
    int out = 0;
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    if ((events & POLLIN) && s->count) out |= POLLIN;
    spinlock_release_irqrestore(&s->lock, f);
    return out;
}

static int64_t sfd_ioctl(vnode_t *n, uint64_t req, void *arg)
{
    signalfd_t *s = (signalfd_t *)n->fs_data;
    if (req == 0x5481) {
        s->nonblock = (arg && *(int *)arg) ? 1 : 0;
        return 0;
    }
    return -ENOTTY;
}

static void sfd_unref(vnode_t *n)
{
    if (--n->refcount > 0) return;
    sfd_unregister((signalfd_t *)n->fs_data);
    free(n->fs_data);
    free(n);
}

static const vnode_ops_t SFD_OPS = {
    .read  = sfd_read,
    .poll  = sfd_poll,
    .ioctl = sfd_ioctl,
    .unref = sfd_unref,
};

int64_t sys_signalfd(uint64_t ufd, uint64_t mask_ptr, uint64_t flags)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;
    if (!mask_ptr) return -EINVAL;

    uint64_t mask = 0;
    if (syscall_copy_from_user(&mask, (const void *)mask_ptr, sizeof mask) < 0)
        return -EFAULT;

    if ((int)ufd >= 0) {
        vfs_file_t *old = fd_get(t->fd_table, (int)ufd);
        if (!old) return -EBADF;
        if (!old->vnode || old->vnode->ops != &SFD_OPS) { fd_put(old); return -EINVAL; }
        signalfd_t *s = (signalfd_t *)old->vnode->fs_data;
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        s->mask = mask;
        spinlock_release_irqrestore(&s->lock, f);
        fd_put(old);
        return (int64_t)ufd;
    }

    signalfd_t *s = calloc(1, sizeof *s);
    if (!s) return -ENOMEM;
    s->mask      = mask;
    s->nonblock  = (flags & SFD_NONBLOCK) ? 1 : 0;
    s->owner_pid = t->pid;
    s->lock = (spinlock_t)SPINLOCK_INIT;

    vnode_t *vn = calloc(1, sizeof *vn);
    if (!vn) { free(s); return -ENOMEM; }
    vn->type     = VFS_NODE_CHARDEV;
    vn->ops      = &SFD_OPS;
    vn->fs_data  = s;
    vn->refcount = 1;

    vfs_file_t *file = vfs_file_alloc();
    if (!file) { free(vn); free(s); return -ENOMEM; }
    file->vnode = vn;
    file->flags = 0;

    sfd_register(s);

    int fd = fd_alloc(t->fd_table, file, 0);
    if (fd < 0) { vfs_file_free(file); return -EMFILE; }
    if (flags & SFD_CLOEXEC) fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    return fd;
}
