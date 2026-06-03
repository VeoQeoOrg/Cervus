#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

int64_t sys_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    int64_t r;
    switch (cmd) {
        case F_GETFD: r = (int64_t)fd_get_flags(t->fd_table, (int)fd); break;
        case F_SETFD: r = (int64_t)fd_set_flags(t->fd_table, (int)fd, (int)arg); break;
        case F_GETFL: r = (int64_t)f->flags; break;
        case F_SETFL: f->flags = (f->flags & O_ACCMODE) | ((int)arg & ~O_ACCMODE); r = 0; break;
        default: r = -EINVAL; break;
    }
    fd_put(f);
    return r;
}
