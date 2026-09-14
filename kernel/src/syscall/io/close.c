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
