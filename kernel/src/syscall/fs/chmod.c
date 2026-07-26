#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/sched/capabilities.h"

int64_t sys_chmod(uint64_t path_ptr, uint64_t mode)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    char kpath[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (rp < 0) return rp;
    if (t->uid != UID_ROOT) {
        vfs_stat_t st;
        if (vfs_stat(kpath, &st) != 0) return -ENOENT;
        if (st.st_uid != t->uid) return -EPERM;
    }
    return vfs_chmod(kpath, (uint32_t)(mode & 0777));
}

int64_t sys_chown(uint64_t path_ptr, uint64_t uid, uint64_t gid)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != UID_ROOT && !cap_has(t->capabilities, CAP_FS_OWNER)) return -EPERM;
    char kpath[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (rp < 0) return rp;
    return vfs_chown(kpath, (uint32_t)uid, (uint32_t)gid);
}
