#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk/blkdev.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static void parse_partition_name(const char *name, char *disk_out, size_t disk_cap,
                                 uint32_t *part_num_out)
{
    size_t nlen = strlen(name);
    size_t end = nlen;
    while (end > 0 && name[end - 1] >= '0' && name[end - 1] <= '9') end--;

    *part_num_out = (end < nlen) ? (uint32_t)atoi(name + end) : 0;

    size_t base_len = end;
    if (base_len > 0 && name[base_len - 1] == 'p' &&
        base_len >= 2 && name[base_len - 2] >= '0' && name[base_len - 2] <= '9') {
        base_len--;
    }
    if (base_len >= disk_cap) base_len = disk_cap - 1;
    memcpy(disk_out, name, base_len);
    disk_out[base_len] = '\0';
}

#define DISK_LIST_PARTS_MAX 64

int64_t sys_disk_list_parts(uint64_t out_ptr, uint64_t max,
                            uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (max == 0) return -EINVAL;
    if (max > DISK_LIST_PARTS_MAX) max = DISK_LIST_PARTS_MAX;
    if (!syscall_uptr_validate((void *)out_ptr, (size_t)max * sizeof(cervus_part_info_t)))
        return -EFAULT;

    cervus_part_info_t out_buf[DISK_LIST_PARTS_MAX];
    cervus_part_info_t *out = out_buf;
    int total = blkdev_count();
    uint64_t written = 0;

    for (int i = 0; i < total && written < max; i++) {
        blkdev_t *d = blkdev_get(i);
        if (!d || !d->present) continue;

        cervus_part_info_t info;
        memset(&info, 0, sizeof(info));
        strncpy(info.part_name, d->name, sizeof(info.part_name) - 1);

        if (d->is_partition) {
            uint32_t pnum = 0;
            parse_partition_name(d->name, info.disk_name, sizeof(info.disk_name), &pnum);
            info.part_num = pnum;
        } else {
            strncpy(info.disk_name, d->name, sizeof(info.disk_name) - 1);
            info.part_num = 0;
        }

        info.size_bytes   = d->size_bytes;
        info.sector_count = d->sector_count;
        info.lba_start    = d->is_partition ? d->part_lba_start : 0;
        info.type         = d->is_partition ? d->part_type      : 0;
        info.bootable     = d->is_partition ? d->part_bootable  : 0;
        memcpy(&out[written], &info, sizeof(info));
        written++;
    }
    if (written > 0) {
        if (syscall_copy_to_user((void *)out_ptr, out_buf,
                                 (size_t)written * sizeof(cervus_part_info_t)) < 0)
            return -EFAULT;
    }
    return (int64_t)written;
}
