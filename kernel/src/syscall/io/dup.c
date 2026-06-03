#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_dup(uint64_t fd)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    int nfd = fd_alloc(t->fd_table, f, 0);
    if (nfd < 0) {
        fd_put(f);
        return -EMFILE;
    }
    return (int64_t)nfd;
}
