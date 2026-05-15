#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/blkdev.h"
#include "../../../include/drivers/partition.h"
#include <string.h>

int64_t sys_disk_partition(uint64_t devname_ptr, uint64_t specs_ptr, uint64_t nparts,
                           uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64];
    if (syscall_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (!syscall_uptr_validate((void *)specs_ptr, sizeof(cervus_mbr_part_t) * nparts)) return -EFAULT;
    if (nparts == 0 || nparts > 4) return -EINVAL;

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;

    cervus_mbr_part_t specs[4];
    memset(specs, 0, sizeof(specs));
    memcpy(specs, (const void *)specs_ptr, sizeof(cervus_mbr_part_t) * nparts);

    mbr_partition_t parts[4];
    memset(parts, 0, sizeof(parts));
    for (uint64_t i = 0; i < nparts; i++) {
        parts[i].boot_flag    = specs[i].boot_flag ? 0x80 : 0x00;
        parts[i].type         = specs[i].type;
        parts[i].lba_start    = specs[i].lba_start;
        parts[i].sector_count = specs[i].sector_count;
        parts[i].chs_start[0] = 0xFE;
        parts[i].chs_start[1] = 0xFF;
        parts[i].chs_start[2] = 0xFF;
        parts[i].chs_end[0]   = 0xFE;
        parts[i].chs_end[1]   = 0xFF;
        parts[i].chs_end[2]   = 0xFF;
    }

    uint32_t sig = 0xCE705CE7;
    int r = partition_write_mbr(dev, parts, sig);
    if (r < 0) return r;
    if (dev->ops->flush) dev->ops->flush(dev);

    partition_scan(dev);
    return 0;
}
