#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

typedef struct {
    uint64_t d_ino;
    uint8_t  d_type;
    char     d_name[256];
} user_dirent_t;

int64_t sys_getdents(uint64_t fd, uint64_t buf_ptr, uint64_t count)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (!buf_ptr) return -EINVAL;

    size_t max_entries = count / sizeof(user_dirent_t);
    if (max_entries == 0) return -EINVAL;

    if (!syscall_uptr_validate((void *)buf_ptr, max_entries * sizeof(user_dirent_t)))
        return -EFAULT;

    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;

    user_dirent_t *out = (user_dirent_t *)buf_ptr;
    size_t written = 0;
    int rc = 0;
    while (written < max_entries) {
        vfs_dirent_t kd;
        int r = vfs_readdir(f, &kd);
        if (r == -ENOTDIR) { rc = -ENOTDIR; break; }
        if (r < 0) break;

        user_dirent_t ent;
        memset(&ent, 0, sizeof(ent));
        ent.d_ino  = kd.d_ino;
        ent.d_type = kd.d_type;
        size_t nl = strnlen(kd.d_name, sizeof(ent.d_name) - 1);
        memcpy(ent.d_name, kd.d_name, nl);
        ent.d_name[nl] = '\0';

        memcpy(&out[written], &ent, sizeof(ent));
        written++;
    }
    fd_put(f);
    if (rc < 0 && written == 0) return rc;
    return (int64_t)(written * sizeof(user_dirent_t));
}
