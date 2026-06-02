#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk/blkdev.h"
#include "../../../include/fs/vfs.h"

int64_t sys_sync(uint64_t a1, uint64_t a2, uint64_t a3,
                 uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;

    vfs_sync_all();

    int n = blkdev_count();
    for (int i = 0; i < n; i++) {
        blkdev_t *d = blkdev_get(i);
        if (!d || !d->present || d->is_partition) continue;
        if (d->ops && d->ops->flush) d->ops->flush(d);
    }
    return 0;
}
