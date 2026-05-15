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
    switch (cmd) {
        case F_GETFD: return (int64_t)fd_get_flags(t->fd_table, (int)fd);
        case F_SETFD: return (int64_t)fd_set_flags(t->fd_table, (int)fd, (int)arg);
        case F_GETFL: return (int64_t)f->flags;
        case F_SETFL: f->flags = (f->flags & O_ACCMODE) | ((int)arg & ~O_ACCMODE); return 0;
        default: return -EINVAL;
    }
}
