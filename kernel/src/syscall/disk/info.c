#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk/blkdev.h"
#include <string.h>

int64_t sys_disk_info(uint64_t index, uint64_t buf_ptr, uint64_t a3,
                      uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!buf_ptr) return -EINVAL;
    if ((int)index >= blkdev_count()) return -ERANGE;
    blkdev_t *dev = blkdev_get((int)index);
    if (!dev) return -ERANGE;
    if (!dev->present) return -ENODEV;
    struct {
        char     name[32];
        uint64_t sectors;
        uint64_t size_bytes;
        char     model[41];
        uint8_t  present;
        uint8_t  is_partition;
        uint8_t  _pad[1];
        uint32_t sector_size;
    } info;
    memset(&info, 0, sizeof(info));
    strncpy(info.name,  dev->name,  31);
    strncpy(info.model, dev->model, 40);
    info.sectors      = dev->sector_count;
    info.size_bytes   = dev->size_bytes;
    info.present      = 1;
    info.is_partition = dev->is_partition ? 1 : 0;
    info.sector_size  = dev->sector_size ? dev->sector_size : 512;
    if (!syscall_uptr_validate((void *)buf_ptr, sizeof(info))) return -EFAULT;
    memcpy((void *)buf_ptr, &info, sizeof(info));
    return 0;
}
