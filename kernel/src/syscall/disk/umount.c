#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk.h"

int64_t sys_disk_umount(uint64_t path_ptr, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0) return -EFAULT;
    return disk_umount(path);
}
