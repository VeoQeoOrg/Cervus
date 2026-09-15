#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/signal/signal.h"

#define F_DUPFD 0
#define F_DUPFD_CLOEXEC 1030
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

extern void unix_set_nonblock(vnode_t *vn, int on);
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7

#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

typedef struct {
    int16_t l_type;
    int16_t l_whence;
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
} user_flock_t;

static int flock_type_in(int16_t t, int *out)
{
    switch (t) {
        case F_RDLCK: *out = VFS_LOCK_READ;   return 0;
        case F_WRLCK: *out = VFS_LOCK_WRITE;  return 0;
        case F_UNLCK: *out = VFS_LOCK_UNLOCK; return 0;
        default: return -EINVAL;
    }
}

static int flock_range(vfs_file_t *f, const user_flock_t *ufl,
                       uint64_t *start_out, uint64_t *end_out)
{
    int64_t base;
    switch (ufl->l_whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)f->offset; break;
        case SEEK_END: {
            vfs_stat_t st;
            if (vfs_fstat(f, &st) < 0) return -EIO;
            base = (int64_t)st.st_size;
            break;
        }
        default: return -EINVAL;
    }

    int64_t start = base + ufl->l_start;
    int64_t len   = ufl->l_len;
    if (len < 0) { start += len; len = -len; }
    if (start < 0) return -EINVAL;

    *start_out = (uint64_t)start;
    *end_out   = (len == 0) ? UINT64_MAX : (uint64_t)start + (uint64_t)len - 1;
    return 0;
}

static int64_t fcntl_lock(task_t *t, vfs_file_t *f, uint64_t cmd, uint64_t arg)
{
    if (!arg) return -EINVAL;
    user_flock_t ufl;
    if (syscall_copy_from_user(&ufl, (const void *)arg, sizeof ufl) < 0)
        return -EFAULT;

    int type;
    int r = flock_type_in(ufl.l_type, &type);
    if (r < 0) return r;

    uint64_t start, end;
    r = flock_range(f, &ufl, &start, &end);
    if (r < 0) return r;

    if (cmd == F_GETLK) {
        if (type == VFS_LOCK_UNLOCK) return -EINVAL;
        vfs_flock_t conflict;
        if (vfs_lock_test(f->vnode, type, start, end, (int)t->pid, &conflict) == 0) {
            ufl.l_type = F_UNLCK;
        } else {
            ufl.l_type   = (conflict.type == VFS_LOCK_WRITE) ? F_WRLCK : F_RDLCK;
            ufl.l_whence = SEEK_SET;
            ufl.l_start  = (int64_t)conflict.start;
            ufl.l_len    = (conflict.end == UINT64_MAX)
                         ? 0 : (int64_t)(conflict.end - conflict.start + 1);
            ufl.l_pid    = conflict.owner;
        }
        return syscall_copy_to_user((void *)arg, &ufl, sizeof ufl);
    }

    for (;;) {
        r = vfs_lock_set(f->vnode, type, start, end, (int)t->pid);
        if (r != -EAGAIN || cmd != F_SETLKW) return r;
        task_sleep_ms(10);
        if (signal_pending_deliverable(t)) return -EINTR;
    }
}

int64_t sys_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    int64_t r;
    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            r = (int64_t)fd_alloc(t->fd_table, f, (int)arg);
            if (r >= 0) {
                __atomic_fetch_add(&f->refcount, 1, __ATOMIC_RELAXED);
                if (cmd == F_DUPFD_CLOEXEC)
                    fd_set_flags(t->fd_table, (int)r, FD_CLOEXEC);
            }
            break;
        case F_GETFD: r = (int64_t)fd_get_flags(t->fd_table, (int)fd); break;
        case F_SETFD: r = (int64_t)fd_set_flags(t->fd_table, (int)fd, (int)arg); break;
        case F_GETFL: r = (int64_t)f->flags; break;
        case F_SETFL:
            f->flags = (f->flags & O_ACCMODE) | ((int)arg & ~O_ACCMODE);
            unix_set_nonblock(f->vnode, (f->flags & O_NONBLOCK) ? 1 : 0);
            r = 0;
            break;
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW: r = fcntl_lock(t, f, cmd, arg); break;
        default: r = -EINVAL; break;
    }
    fd_put(f);
    return r;
}
