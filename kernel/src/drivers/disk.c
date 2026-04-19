#include "../../include/drivers/disk.h"
#include "../../include/drivers/ata.h"
#include "../../include/drivers/blkdev.h"
#include "../../include/drivers/partition.h"
#include "../../include/fs/ext2.h"
#include "../../include/fs/fat32.h"
#include "../../include/fs/vfs.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/errno.h"
#include <stdio.h>
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
extern void devfs_register(const char *name, vnode_t *node);

static int64_t blk_vnode_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    if (!dev) return -EIO;
    int r = blkdev_read(dev, offset, buf, len);
    return (r < 0) ? r : (int64_t)len;
}
static int64_t blk_vnode_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    if (!dev) return -EIO;
    int r = blkdev_write(dev, offset, buf, len);
    return (r < 0) ? r : (int64_t)len;
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
    .read = blk_vnode_read, .write = blk_vnode_write,
    .stat = blk_vnode_stat, .ref = blk_vnode_ref, .unref = blk_vnode_unref,
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
                      names[i], drv->model, drv->size_bytes / (1024 * 1024));
        printf("[disk] /dev/%s -> %s (%llu MB)\n",
                      names[i], drv->model, drv->size_bytes / (1024 * 1024));

        partition_scan(bdev);
        count++;
    }
    if (count == 0) serial_writestring("[disk] no disks available\n");
    else { serial_printf("[disk] %d disk(s) ready\n", count); printf("[disk] %d disk(s) ready\n", count); }
}

static const char *strip_dev_prefix(const char *name) {
    if (strncmp(name, "/dev/", 5) == 0) return name + 5;
    return name;
}

int disk_format(const char *devname, const char *label) {
    const char *raw = strip_dev_prefix(devname);
    blkdev_t *dev = blkdev_get_by_name(raw);
    if (!dev) return -ENODEV;
    return ext2_format(dev, label ? label : raw);
}

static int detect_fs_type(blkdev_t *dev) {
    uint8_t sec[512];
    if (dev->ops->read_sectors(dev, 0, 1, sec) < 0) return -1;
    if (sec[510] == 0x55 && sec[511] == (uint8_t)0xAA) {
        if (memcmp(sec + 82, "FAT32", 5) == 0) return 1;
        if (memcmp(sec + 54, "FAT", 3) == 0)   return 1;
    }
    uint16_t magic = 0;
    if (blkdev_read(dev, EXT2_SUPER_OFFSET + 56, &magic, sizeof(magic)) == 0
        && magic == EXT2_SUPER_MAGIC) return 2;
    return 0;
}

int disk_mount(const char *devname, const char *path) {
    const char *raw = strip_dev_prefix(devname);
    blkdev_t *dev = blkdev_get_by_name(raw);
    if (!dev) return -ENODEV;

    int t = detect_fs_type(dev);
    vnode_t *root = NULL;
    if (t == 1) {
        root = fat32_mount(dev);
        serial_printf("[disk] mounting FAT32 %s -> %s\n", raw, path);
    } else if (t == 2) {
        root = ext2_mount(dev);
        serial_printf("[disk] mounting ext2 %s -> %s\n", raw, path);
    } else {
        serial_printf("[disk] %s: no recognizable FS\n", raw);
        return -EINVAL;
    }
    if (!root) return -EIO;
    int r = vfs_mount(path, root);
    if (r < 0) { vnode_unref(root); return r; }

    const char *fsname = (t == 1) ? "fat32" : "ext2";
    int si = vfs_set_mount_info(path, raw, fsname);
    serial_printf("[disk_mount] set_mount_info path='%s' dev='%s' fs='%s' -> %d\n",
                  path, raw, fsname, si);
    return 0;
}

int disk_umount(const char *path) {
    return vfs_umount(path);
}