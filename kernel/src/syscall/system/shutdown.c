#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/acpi/acpi.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/drivers/disk/blkdev.h"
#include "../../../include/io/serial.h"

int64_t sys_shutdown(void)
{
    task_t *t = syscall_cur_task();
    if (t && t->uid != 0) return -EPERM;
    serial_writestring("[SYSCALL] shutdown requested\n");
    vfs_sync_all();
    int n = blkdev_count();
    for (int i = 0; i < n; i++) {
        blkdev_t *d = blkdev_get(i);
        if (!d || !d->present || d->is_partition) continue;
        if (d->ops && d->ops->flush) d->ops->flush(d);
    }
    acpi_shutdown();
    return 0;
}
