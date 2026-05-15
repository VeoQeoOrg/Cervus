#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/blkdev.h"
#include "../../../include/drivers/ata.h"
#include <string.h>

int64_t sys_disk_info(uint64_t index, uint64_t buf_ptr, uint64_t a3,
                      uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!buf_ptr) return -EINVAL;
    blkdev_t *dev = blkdev_get((int)index);
    if (!dev || !dev->present) return -ENODEV;
    struct {
        char     name[32];
        uint64_t sectors;
        uint64_t size_bytes;
        char     model[41];
        uint8_t  present;
        uint8_t  _pad[6];
    } info;
    memset(&info, 0, sizeof(info));
    strncpy(info.name, dev->name, 31);
    info.sectors    = dev->sector_count;
    info.size_bytes = dev->size_bytes;
    info.present    = 1;
    ata_drive_t *ata = (ata_drive_t *)dev->priv;
    if (ata) strncpy(info.model, ata->model, 40);
    if (!syscall_uptr_validate((void *)buf_ptr, sizeof(info))) return -EFAULT;
    memcpy((void *)buf_ptr, &info, sizeof(info));
    return 0;
}
