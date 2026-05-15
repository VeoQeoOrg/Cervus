#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk.h"
#include "../../../include/io/serial.h"

int64_t sys_disk_mount(uint64_t devname_ptr, uint64_t path_ptr,
                       uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64], path[256];
    if (syscall_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (syscall_strncpy_from_user(path,    (const char *)path_ptr,    sizeof(path))    < 0) return -EFAULT;
    serial_printf("[SYSCALL] disk_mount('%s', '%s') by pid=%u\n", devname, path, t->pid);
    return disk_mount(devname, path);
}
