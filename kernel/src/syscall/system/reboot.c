#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/acpi/acpi.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/io/serial.h"

int64_t sys_reboot(void)
{
    task_t *t = syscall_cur_task();
    if (t && t->uid != 0) return -EPERM;
    serial_writestring("[SYSCALL] reboot requested\n");
    vfs_sync_all();
    acpi_reboot();
    return 0;
}
