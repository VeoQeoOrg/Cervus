#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/drivers/timer.h"

extern int64_t clock_realtime_sec(void);

int64_t sys_utimes(uint64_t path_ptr, uint64_t atime, uint64_t mtime,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    char path[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(path, (const char *)path_ptr, sizeof(path));
    if (rp < 0) return rp;

    int pf = syscall_perm_file(path, 2);
    if (pf < 0) return pf;

    int64_t at = (int64_t)atime;
    int64_t mt = (int64_t)mtime;
    if (at < 0 || mt < 0) {
        int64_t now = clock_realtime_sec();
        if (at < 0) at = now;
        if (mt < 0) mt = now;
    }

    int r = vfs_set_times(path, at, mt);
    if (r == 0) vfs_sync_all();
    return r;
}
