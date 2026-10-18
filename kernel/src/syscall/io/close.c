#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_close(uint64_t fd)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (f) {
        if (f->vnode) vfs_lock_release_vnode(f->vnode, (int)t->pid);
        fd_put(f);
    }
    return (int64_t)fd_close(t->fd_table, (int)fd);
}

#define CLOSE_RANGE_UNSHARE (1u << 1)
#define CLOSE_RANGE_CLOEXEC (1u << 2)

int64_t sys_close_range(uint64_t first, uint64_t last, uint64_t flags)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (flags & ~(uint64_t)(CLOSE_RANGE_UNSHARE | CLOSE_RANGE_CLOEXEC)) return -EINVAL;
    if ((uint32_t)first > (uint32_t)last) return -EINVAL;
    if ((flags & CLOSE_RANGE_UNSHARE) && t->fd_table->refs > 1) {
        fd_table_t *own = fd_table_clone(t->fd_table);
        if (!own) return -ENOMEM;
        fd_table_t *old = t->fd_table;
        t->fd_table = own;
        fd_table_destroy(old);
    }
    uint64_t end = (uint64_t)(uint32_t)last + 1;
    uint64_t cap = (uint64_t)fd_table_capacity(t->fd_table);
    if (end > cap) end = cap;
    for (uint64_t fd = (uint32_t)first; fd < end; fd++) {
        if (flags & CLOSE_RANGE_CLOEXEC) {
            int fl = fd_get_flags(t->fd_table, (int)fd);
            if (fl >= 0) fd_set_flags(t->fd_table, (int)fd, fl | FD_CLOEXEC);
        } else {
            sys_close(fd);
        }
    }
    return 0;
}
