#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/blkdev.h"
#include "../../../include/fs/fat32.h"
#include <string.h>

int64_t sys_disk_mkfs_fat32(uint64_t devname_ptr, uint64_t label_ptr,
                            uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64], label[16];
    if (syscall_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (label_ptr) {
        if (syscall_strncpy_from_user(label, (const char *)label_ptr, sizeof(label)) < 0) return -EFAULT;
    } else {
        strncpy(label, "CERVUS", sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
    }

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;

    return fat32_format(dev, label);
}
