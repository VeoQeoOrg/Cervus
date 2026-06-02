#include "../../../include/drivers/disk/disk.h"
#include "../../../include/drivers/disk/ata.h"
#include "../../../include/drivers/disk/ahci.h"
#include "../../../include/drivers/disk/nvme.h"
#include "../../../include/drivers/disk/blkdev.h"
#include "../../../include/drivers/disk/partition.h"
#include "../../../include/fs/ext2.h"
#include "../../../include/fs/fat32.h"
#include "../../../include/fs/iso9660.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/io/serial.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/syscall/errno.h"
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

static int ahci_blk_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    return ahci_read_sectors((ahci_device_t *)dev->priv, lba, count, buf);
}
static int ahci_blk_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    return ahci_write_sectors((ahci_device_t *)dev->priv, lba, count, buf);
}
static int ahci_blk_flush(blkdev_t *dev) {
    return ahci_flush((ahci_device_t *)dev->priv);
}
static const blkdev_ops_t ahci_blkdev_ops = {
    .read_sectors  = ahci_blk_read,
    .write_sectors = ahci_blk_write,
    .flush         = ahci_blk_flush,
};

static int nvme_blk_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    return nvme_read_sectors((nvme_namespace_t *)dev->priv, lba, count, buf);
}
static int nvme_blk_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    return nvme_write_sectors((nvme_namespace_t *)dev->priv, lba, count, buf);
}
static int nvme_blk_flush(blkdev_t *dev) {
    return nvme_flush((nvme_namespace_t *)dev->priv);
}
static const blkdev_ops_t nvme_blkdev_ops = {
    .read_sectors  = nvme_blk_read,
    .write_sectors = nvme_blk_write,
    .flush         = nvme_blk_flush,
};

#define NVME_MAX_TOTAL_NS (NVME_MAX_CONTROLLERS * NVME_MAX_NAMESPACES)

static blkdev_t g_ata_blkdevs[ATA_MAX_DRIVES];
static blkdev_t g_ahci_blkdevs[AHCI_MAX_DEVICES];
static blkdev_t g_nvme_blkdevs[NVME_MAX_TOTAL_NS];
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

#define MAX_DISK_VNODES (ATA_MAX_DRIVES + AHCI_MAX_DEVICES + NVME_MAX_TOTAL_NS)
static vnode_t g_blk_vnodes[MAX_DISK_VNODES];
static uint64_t g_blk_ino_base = 200;

void disk_init(void) {
    serial_writestring("[disk] initializing...\n");
    blkdev_init();
    ahci_init();
    nvme_init();
    ata_init();
    int count = 0;
    const char *names[] = { "hda", "hdb", "hdc", "hdd" };
    for (int i = 0; i < ATA_MAX_DRIVES; i++) {
        ata_drive_t *drv = ata_get_drive(i);
        if (!drv) continue;
        blkdev_t *bdev = &g_ata_blkdevs[count];
        memset(bdev, 0, sizeof(*bdev));
        strncpy(bdev->name, names[i], BLKDEV_NAME_MAX - 1);
        strncpy(bdev->model, drv->model, BLKDEV_MODEL_MAX - 1);
        bdev->present      = true;
        bdev->is_partition = false;
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

        partition_scan(bdev);
        count++;
    }

    const char *sd_names[] = { "sda", "sdb", "sdc", "sdd", "sde", "sdf", "sdg", "sdh" };
    const char *sr_names[] = { "sr0", "sr1", "sr2", "sr3" };
    int sd_idx = 0, sr_idx = 0;
    int n_ahci = ahci_device_count();
    for (int i = 0; i < n_ahci && (sd_idx + sr_idx) < AHCI_MAX_DEVICES; i++) {
        ahci_device_t *adev = ahci_get_device(i);
        if (!adev || !adev->present) continue;

        const char *name;
        int slot;
        if (adev->atapi) {
            if (sr_idx >= (int)(sizeof(sr_names) / sizeof(sr_names[0]))) continue;
            name = sr_names[sr_idx];
            slot = sd_idx + sr_idx;
            sr_idx++;
        } else {
            if (sd_idx >= (int)(sizeof(sd_names) / sizeof(sd_names[0]))) continue;
            name = sd_names[sd_idx];
            slot = sd_idx + sr_idx;
            sd_idx++;
        }

        blkdev_t *bdev = &g_ahci_blkdevs[slot];
        memset(bdev, 0, sizeof(*bdev));
        strncpy(bdev->name, name, BLKDEV_NAME_MAX - 1);
        strncpy(bdev->model, adev->model, BLKDEV_MODEL_MAX - 1);
        bdev->present      = true;
        bdev->is_partition = false;
        bdev->sector_count = adev->sectors;
        bdev->size_bytes   = adev->size_bytes;
        bdev->sector_size  = adev->atapi ? ATAPI_SECTOR_SIZE : AHCI_SECTOR_SIZE;
        bdev->ops          = &ahci_blkdev_ops;
        bdev->priv         = adev;
        blkdev_register(bdev);

        int vn_slot = ATA_MAX_DRIVES + slot;
        vnode_t *vn = &g_blk_vnodes[vn_slot];
        memset(vn, 0, sizeof(*vn));
        vn->type     = VFS_NODE_BLKDEV;
        vn->mode     = 0660;
        vn->ino      = g_blk_ino_base + (uint64_t)vn_slot;
        vn->ops      = &blk_vnode_ops;
        vn->fs_data  = bdev;
        vn->size     = adev->size_bytes;
        vn->refcount = 1;
        devfs_register(name, vn);

        serial_printf("[disk] /dev/%s -> AHCI %s (%llu MB)%s\n",
                      name, adev->model,
                      adev->size_bytes / (1024 * 1024),
                      adev->atapi ? " [CDROM]" : "");

        if (!adev->atapi) partition_scan(bdev);
        count++;
    }

    int n_nvme = nvme_namespace_count();
    for (int i = 0; i < n_nvme && i < NVME_MAX_TOTAL_NS; i++) {
        nvme_namespace_t *ns = nvme_get_namespace(i);
        if (!ns || !ns->active) continue;

        blkdev_t *bdev = &g_nvme_blkdevs[i];
        memset(bdev, 0, sizeof(*bdev));
        strncpy(bdev->name, ns->name, BLKDEV_NAME_MAX - 1);
        strncpy(bdev->model, nvme_controller_model(ns), BLKDEV_MODEL_MAX - 1);
        bdev->present      = true;
        bdev->is_partition = false;
        bdev->sector_count = ns->sectors;
        bdev->size_bytes   = ns->sectors * (uint64_t)ns->lba_size;
        bdev->sector_size  = ns->lba_size;
        bdev->ops          = &nvme_blkdev_ops;
        bdev->priv         = ns;
        blkdev_register(bdev);

        int vn_slot = ATA_MAX_DRIVES + AHCI_MAX_DEVICES + i;
        vnode_t *vn = &g_blk_vnodes[vn_slot];
        memset(vn, 0, sizeof(*vn));
        vn->type     = VFS_NODE_BLKDEV;
        vn->mode     = 0660;
        vn->ino      = g_blk_ino_base + (uint64_t)vn_slot;
        vn->ops      = &blk_vnode_ops;
        vn->fs_data  = bdev;
        vn->size     = bdev->size_bytes;
        vn->refcount = 1;
        devfs_register(ns->name, vn);

        serial_printf("[disk] /dev/%s -> NVMe %s (%llu MB)\n",
                      ns->name, nvme_controller_model(ns),
                      bdev->size_bytes / (1024 * 1024));

        partition_scan(bdev);
        count++;
    }

    if (count == 0) serial_writestring("[disk] no disks available\n");
    else serial_printf("[disk] %d disk(s) ready\n", count);
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
    uint8_t cd_sig[6] = {0};
    if (blkdev_read(dev, 32768, cd_sig, 6) == 0) {
        if (cd_sig[0] == 0x01 &&
            cd_sig[1] == 'C' && cd_sig[2] == 'D' &&
            cd_sig[3] == '0' && cd_sig[4] == '0' && cd_sig[5] == '1') {
            return 3;
        }
    }

    uint8_t bpb_head[90] = {0};
    if (blkdev_read(dev, 0, bpb_head, sizeof(bpb_head)) == 0) {
        uint8_t mbr_tail[2] = {0};
        if (blkdev_read(dev, 510, mbr_tail, 2) == 0
            && mbr_tail[0] == 0x55 && mbr_tail[1] == 0xAA) {
            if (memcmp(bpb_head + 82, "FAT32", 5) == 0) return 1;
            if (memcmp(bpb_head + 54, "FAT",   3) == 0) return 1;
        }
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
    const char *fsname = NULL;
    if (t == 1) {
        root = fat32_mount(dev);
        fsname = "fat32";
        serial_printf("[disk] mounting FAT32 %s -> %s\n", raw, path);
    } else if (t == 2) {
        root = ext2_mount(dev);
        fsname = "ext2";
        serial_printf("[disk] mounting ext2 %s -> %s\n", raw, path);
    } else if (t == 3) {
        root = iso9660_mount(dev);
        fsname = "iso9660";
        serial_printf("[disk] mounting iso9660 %s -> %s\n", raw, path);
    } else {
        serial_printf("[disk] %s: no recognizable FS\n", raw);
        return -EINVAL;
    }
    if (!root) return -EIO;
    int r = vfs_mount(path, root);
    if (r < 0) { vnode_unref(root); return r; }

    int si = vfs_set_mount_info(path, raw, fsname);
    serial_printf("[disk_mount] set_mount_info path='%s' dev='%s' fs='%s' -> %d\n",
                  path, raw, fsname, si);
    return 0;
}

int disk_umount(const char *path) {
    return vfs_umount(path);
}