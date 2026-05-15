#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_open(uint64_t path_ptr, uint64_t flags, uint64_t mode)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (!t->fd_table) return -ENOMEM;

    char kpath[VFS_MAX_PATH];
    if (syscall_strncpy_from_user(kpath, (const char *)path_ptr, sizeof(kpath)) < 0) return -EFAULT;
    if (!kpath[0]) return -ENOENT;

    vfs_file_t *file = NULL;
    int ret = vfs_open(kpath, (int)flags, (uint32_t)mode, &file);
    if (ret < 0) return (int64_t)ret;

    int newfd = fd_alloc(t->fd_table, file, 0);
    if (newfd < 0) { vfs_close(file); return -EMFILE; }
    return (int64_t)newfd;
}
