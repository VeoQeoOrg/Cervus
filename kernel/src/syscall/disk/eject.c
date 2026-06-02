#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk/blkdev.h"
#include "../../../include/drivers/disk/ahci.h"
#include <string.h>

int64_t sys_disk_eject(uint64_t devname_ptr, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64];
    if (syscall_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0)
        return -EFAULT;

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;

    if (dev->sector_size != 2048) return -EOPNOTSUPP;

    ahci_device_t *adev = (ahci_device_t *)dev->priv;
    if (!adev || !adev->atapi) return -EOPNOTSUPP;

    return ahci_eject(adev);
}
