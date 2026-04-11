#include "../../include/drivers/disk.h"
#include "../../include/drivers/ata.h"
#include "../../include/drivers/blkdev.h"
#include "../../include/fs/cervusfs.h"
#include "../../include/fs/vfs.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/errno.h"
#include <string.h>

static int ata_blk_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    ata_drive_t *drv = (ata_drive_t *)dev->priv;
    return ata_read_sectors(drv, lba, count, buf);
}

static int ata_blk_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    ata_drive_t *drv = (ata_drive_t *)dev->priv;
    return ata_write_sectors(drv, lba, count, buf);
}

static int ata_blk_flush(blkdev_t *dev) {
    ata_drive_t *drv = (ata_drive_t *)dev->priv;
    return ata_flush(drv);
}

static const blkdev_ops_t ata_blkdev_ops = {
    .read_sectors  = ata_blk_read,
    .write_sectors = ata_blk_write,
    .flush         = ata_blk_flush,
};

static blkdev_t g_ata_blkdevs[ATA_MAX_DRIVES];

extern vnode_t *devfs_create_root(void);
extern void devfs_register(const char *name, vnode_t *node);

static int64_t blk_vnode_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    if (!dev) return -EIO;
    int r = blkdev_read(dev, offset, buf, len);
    if (r < 0) return r;
    return (int64_t)len;
}

static int64_t blk_vnode_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    if (!dev) return -EIO;
    int r = blkdev_write(dev, offset, buf, len);
    if (r < 0) return r;
    return (int64_t)len;
}

static int blk_vnode_stat(vnode_t *node, vfs_stat_t *out) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_BLKDEV;
    out->st_mode = 0660;
    out->st_size = dev ? dev->size_bytes : 0;
    return 0;
}

static void blk_vnode_ref(vnode_t *n)   { (void)n; }
static void blk_vnode_unref(vnode_t *n) { (void)n; }

static const vnode_ops_t blk_vnode_ops = {
    .read   = blk_vnode_read,
    .write  = blk_vnode_write,
    .stat   = blk_vnode_stat,
    .ref    = blk_vnode_ref,
    .unref  = blk_vnode_unref,
};

static vnode_t g_blk_vnodes[ATA_MAX_DRIVES];
static uint64_t g_blk_ino_base = 200;

void disk_init(void) {
    serial_writestring("[disk] initializing...\n");
    blkdev_init();
    ata_init();
    int count = 0;
    const char *names[] = { "hda", "hdb", "hdc", "hdd" };
    for (int i = 0; i < ATA_MAX_DRIVES; i++) {
        ata_drive_t *drv = ata_get_drive(i);
        if (!drv) continue;
        blkdev_t *bdev = &g_ata_blkdevs[count];
        memset(bdev, 0, sizeof(*bdev));
        strncpy(bdev->name, names[i], BLKDEV_NAME_MAX - 1);
        bdev->present      = true;
        bdev->sector_count = drv->sectors;
        bdev->size_bytes   = drv->size_bytes;
        bdev->sector_size  = ATA_SECTOR_SIZE;
        bdev->ops          = &ata_blkdev_ops;
        bdev->priv         = drv;
        blkdev_register(bdev);
        vnode_t *vn = &g_blk_vnodes[count];
        memset(vn, 0, sizeof(*vn));
        vn->type     = VFS_NODE_BLKDEV;
        vn->mode     = 0660;
        vn->ino      = g_blk_ino_base + (uint64_t)count;
        vn->ops      = &blk_vnode_ops;
        vn->fs_data  = bdev;
        vn->size     = drv->size_bytes;
        vn->refcount = 1;
        devfs_register(names[i], vn);
        serial_printf("[disk] /dev/%s -> %s (%llu MB)\n",
                      names[i], drv->model,
                      drv->size_bytes / (1024 * 1024));
        count++;
    }
    if (count == 0)
        serial_writestring("[disk] no disks available\n");
    else
        serial_printf("[disk] %d disk(s) ready\n", count);
}

int disk_format(const char *devname, const char *label) {
    blkdev_t *dev = blkdev_get_by_name(devname);
    if (!dev) return -ENODEV;
    return cervusfs_format(dev, label ? label : devname);
}

static void disk_cervusfs_unmount_cb(void *fs_priv) {
    cervusfs_unmount((cervusfs_t *)fs_priv);
}

static void disk_cervusfs_sync_cb(void *fs_priv) {
    cervusfs_sync((cervusfs_t *)fs_priv);
}

int disk_mount(const char *devname, const char *path) {
    if (!devname || !path) return -EINVAL;
    blkdev_t *dev = blkdev_get_by_name(devname);
    if (!dev) {
        serial_printf("[disk] mount: device '%s' not found\n", devname);
        return -ENODEV;
    }
    vnode_t *root = cervusfs_mount(dev);
    if (!root) {
        serial_printf("[disk] mount: '%s' has no valid CervusFS, formatting...\n", devname);
        int r = cervusfs_format(dev, devname);
        if (r < 0) return r;
        root = cervusfs_mount(dev);
        if (!root) return -EIO;
    }
    cervusfs_t *cfs = *(cervusfs_t **)root->fs_data;

    if (root->ops && root->ops->mkdir) {
        int hr = root->ops->mkdir(root, "home", 0755);
        if (hr == 0) {
            serial_writestring("[disk] created 'home' on new filesystem\n");
            cervusfs_sync(cfs);
        } else if (hr == -17) {
            serial_writestring("[disk] /mnt/home exists\n");
        } else {
            serial_printf("[disk] WARNING: could not create 'home': %d\n", hr);
        }
    }
    vnode_t *check = NULL;
    if (vfs_lookup(path, &check) < 0) {
        vfs_mkdir(path, 0755);
    } else {
        vnode_unref(check);
    }
    int r = vfs_mount_fs(path, root, cfs, disk_cervusfs_unmount_cb, disk_cervusfs_sync_cb);
    if (r < 0) {
        serial_printf("[disk] mount: vfs_mount_fs('%s') failed: %d\n", path, r);
        vnode_unref(root);
        return r;
    }
    serial_printf("[disk] mounted '%s' at '%s'\n", devname, path);
    return 0;
}

int disk_umount(const char *path) {
    return vfs_umount(path);
}