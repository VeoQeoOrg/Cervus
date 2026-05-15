#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_seek(uint64_t fd, uint64_t offset, uint64_t whence)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    return vfs_seek(f, (int64_t)offset, (int)whence);
}
