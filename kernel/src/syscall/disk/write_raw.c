#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk/blkdev.h"
#include <string.h>

int64_t sys_disk_write_raw(uint64_t devname_ptr, uint64_t lba, uint64_t count,
                           uint64_t buf_ptr, uint64_t a5, uint64_t a6)
{
    (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64];
    if (syscall_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (count == 0 || count > 256) return -EINVAL;

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;
    if (dev->sector_size == 0) return -EIO;
    if (lba > dev->sector_count || count > dev->sector_count - lba) return -EINVAL;

    uint64_t bytes = count * (uint64_t)dev->sector_size;
    if (bytes / (uint64_t)dev->sector_size != count) return -EINVAL;
    if (!syscall_uptr_validate((void *)buf_ptr, (size_t)bytes)) return -EFAULT;

    int r = dev->ops->write_sectors(dev, lba, (uint32_t)count, (const void *)buf_ptr);
    if (r < 0) return r;
    if (dev->ops->flush) dev->ops->flush(dev);
    return (int64_t)bytes;
}
