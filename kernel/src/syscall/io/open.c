#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

int64_t sys_open(uint64_t path_ptr, uint64_t flags, uint64_t mode)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (!t->fd_table) return -ENOMEM;

    char kpath[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (rp < 0) return rp;
    if (!kpath[0]) return -ENOENT;

    if (t->uid != 0) {
        int want = 0;
        int acc = (int)(flags & 3);
        if (acc == O_RDONLY || acc == O_RDWR) want |= 4;
        if (acc == O_WRONLY || acc == O_RDWR || (flags & (O_TRUNC | O_APPEND))) want |= 2;

        int pr = syscall_perm_file(kpath, want);
        if (pr == -EACCES) return -EACCES;
        if (pr == -ENOENT && (flags & O_CREAT)) {
            int pp = syscall_perm_parent(kpath, 2);
            if (pp < 0) return pp;
        }
    }

    vfs_file_t *file = NULL;
    int ret = vfs_open(kpath, (int)flags, (uint32_t)mode, &file);
    if (ret < 0) return (int64_t)ret;

    int newfd = fd_alloc(t->fd_table, file, 0);
    if (newfd < 0) { vfs_close(file); return -EMFILE; }
    return (int64_t)newfd;
}
