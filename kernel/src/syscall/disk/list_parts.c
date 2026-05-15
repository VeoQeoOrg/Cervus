#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/blkdev.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

int64_t sys_disk_list_parts(uint64_t out_ptr, uint64_t max,
                            uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_uptr_validate((void *)out_ptr, sizeof(cervus_part_info_t))) return -EFAULT;
    if (max == 0) return -EINVAL;

    cervus_part_info_t *out = (cervus_part_info_t *)out_ptr;
    int total = blkdev_count();
    uint64_t written = 0;

    for (int i = 0; i < total && written < max; i++) {
        blkdev_t *d = blkdev_get(i);
        if (!d || !d->present) continue;

        size_t nlen = strlen(d->name);
        bool is_part = false;
        if (nlen >= 2) {
            for (size_t k = 0; k < nlen; k++) {
                if (d->name[k] >= '0' && d->name[k] <= '9') { is_part = true; break; }
            }
        }
        cervus_part_info_t info;
        memset(&info, 0, sizeof(info));
        strncpy(info.part_name, d->name, sizeof(info.part_name) - 1);
        if (is_part) {
            char base[32]; int bi = 0;
            for (size_t k = 0; k < nlen && bi < 31; k++) {
                if (d->name[k] >= '0' && d->name[k] <= '9') break;
                base[bi++] = d->name[k];
            }
            base[bi] = '\0';
            strncpy(info.disk_name, base, sizeof(info.disk_name) - 1);
            info.part_num = (uint32_t)atoi(d->name + bi);
        } else {
            strncpy(info.disk_name, d->name, sizeof(info.disk_name) - 1);
            info.part_num = 0;
        }
        info.size_bytes   = d->size_bytes;
        info.sector_count = d->sector_count;
        info.lba_start    = 0;
        info.type         = 0;
        info.bootable     = 0;
        memcpy(&out[written], &info, sizeof(info));
        written++;
    }
    return (int64_t)written;
}
