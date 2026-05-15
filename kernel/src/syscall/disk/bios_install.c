#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/blkdev.h"
#include "../../../include/io/serial.h"
#include <string.h>

int64_t sys_disk_bios_install(uint64_t a1, uint64_t a2, uint64_t a3,
                              uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    const char *disk_name = (const char *)a1;
    const void *sys_data  = (const void *)a2;
    uint32_t    sys_size  = (uint32_t)a3;

    if (!disk_name || !sys_data || sys_size < 512) return -EINVAL;

    blkdev_t *dev = blkdev_get_by_name(disk_name);
    if (!dev) return -ENOENT;
    if (dev->sector_size != 512) return -EINVAL;

    uint8_t sector0[512];
    int r = dev->ops->read_sectors(dev, 0, 1, sector0);
    if (r < 0) return r;

    uint8_t saved_timestamp[6];
    uint8_t saved_parttable[70];
    memcpy(saved_timestamp, sector0 + 218, 6);
    memcpy(saved_parttable, sector0 + 440, 70);

    const uint8_t *src = (const uint8_t *)sys_data;

    memcpy(sector0, src, 512);

    memcpy(sector0 + 218, saved_timestamp, 6);
    memcpy(sector0 + 440, saved_parttable, 70);

    uint64_t stage2_loc = 512;
    memcpy(sector0 + 0x1A4, &stage2_loc, 8);

    uint32_t stage2_bytes   = sys_size - 512;
    uint32_t stage2_sectors = (stage2_bytes + 511) / 512;

    if (1 + stage2_sectors >= 2048) return -ENOSPC;

    uint8_t sector_buf[512];
    for (uint32_t i = 0; i < stage2_sectors; i++) {
        uint32_t off = i * 512;
        uint32_t take = (stage2_bytes - off >= 512) ? 512 : (stage2_bytes - off);
        memset(sector_buf, 0, 512);
        memcpy(sector_buf, src + 512 + off, take);
        r = dev->ops->write_sectors(dev, 1 + i, 1, sector_buf);
        if (r < 0) return r;
    }

    r = dev->ops->write_sectors(dev, 0, 1, sector0);
    if (r < 0) return r;

    if (dev->ops->flush) dev->ops->flush(dev);

    serial_printf("[bios-install] deployed: stage1=512B at LBA 0, stage2=%uB at LBA 1..%u\n",
                  stage2_bytes, stage2_sectors);
    return 0;
}
