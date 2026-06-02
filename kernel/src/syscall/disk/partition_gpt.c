#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk/blkdev.h"
#include "../../../include/drivers/disk/partition.h"
#include "../../../include/memory/pmm.h"
#include <string.h>

struct user_gpt_spec {
    uint8_t  type_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    char     name[36];
};

int64_t sys_disk_partition_gpt(uint64_t devname_ptr, uint64_t specs_ptr, uint64_t nparts,
                               uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    if (nparts == 0 || nparts > 128) return -EINVAL;

    char devname[64];
    if (syscall_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0)
        return -EFAULT;

    if (!syscall_uptr_validate((void *)specs_ptr,
                               sizeof(struct user_gpt_spec) * (size_t)nparts))
        return -EFAULT;

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;

    gpt_partition_spec_t *specs =
        kmalloc(sizeof(gpt_partition_spec_t) * (size_t)nparts);
    if (!specs) return -ENOMEM;

    const struct user_gpt_spec *u = (const struct user_gpt_spec *)specs_ptr;
    for (uint64_t i = 0; i < nparts; i++) {
        memcpy(specs[i].type_guid, u[i].type_guid, 16);
        specs[i].first_lba = u[i].first_lba;
        specs[i].last_lba  = u[i].last_lba;
        memcpy(specs[i].name, u[i].name, sizeof(specs[i].name));
    }

    int r = partition_write_gpt(dev, specs, (size_t)nparts, NULL);
    kfree(specs);
    if (r < 0) return r;

    if (dev->ops && dev->ops->flush) dev->ops->flush(dev);
    partition_scan(dev);
    return 0;
}
