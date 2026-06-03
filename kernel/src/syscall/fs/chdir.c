#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

int64_t sys_chdir(uint64_t path_ptr, uint64_t a2, uint64_t a3,
                  uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;

    char abs[VFS_MAX_PATH];
    int r = syscall_resolve_path_from_user(abs, (const char *)path_ptr, sizeof(abs));
    if (r < 0) return r;

    vfs_stat_t st;
    int sr = vfs_stat(abs, &st);
    if (sr < 0) return sr;
    if (st.st_type != VFS_NODE_DIR) return -ENOTDIR;

    size_t L = strlen(abs);
    if (L >= sizeof(t->cwd)) return -ENAMETOOLONG;
    memcpy(t->cwd, abs, L + 1);
    return 0;
}

int64_t sys_getcwd(uint64_t buf_ptr, uint64_t size, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (!buf_ptr || size == 0) return -EINVAL;

    const char *cwd = t->cwd[0] ? t->cwd : "/";
    size_t L = strlen(cwd) + 1;
    if (L > size) return -ERANGE;
    return syscall_copy_to_user((void *)buf_ptr, cwd, L);
}
