#include "../../include/drivers/partition.h"
#include "../../include/drivers/blkdev.h"
#include "../../include/fs/vfs.h"
#include "../../include/io/serial.h"
#include "../../include/syscall/errno.h"
#include "../../include/memory/pmm.h"
#include <string.h>
#include <stdio.h>

#define MAX_PARTITIONS 16

typedef struct {
    blkdev_t base;
    blkdev_t *parent;
    uint64_t offset_sectors;
    uint64_t count_sectors;
    uint8_t  type;
    uint8_t  bootable;
    uint32_t partnum;
} partition_blkdev_t;

static partition_blkdev_t g_partitions[MAX_PARTITIONS];
static int g_partition_count = 0;

extern void devfs_register(const char *name, vnode_t *node);

static int64_t part_vnode_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    if (!dev) return -EIO;
    int r = blkdev_read(dev, offset, buf, len);
    return (r < 0) ? r : (int64_t)len;
}
static int64_t part_vnode_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    if (!dev) return -EIO;
    int r = blkdev_write(dev, offset, buf, len);
    return (r < 0) ? r : (int64_t)len;
}
static int part_vnode_stat(vnode_t *node, vfs_stat_t *out) {
    blkdev_t *dev = (blkdev_t *)node->fs_data;
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_BLKDEV;
    out->st_mode = 0660;
    out->st_size = dev ? dev->size_bytes : 0;
    return 0;
}
static void part_vnode_ref(vnode_t *n)   { (void)n; }
static void part_vnode_unref(vnode_t *n) { (void)n; }

static const vnode_ops_t part_vnode_ops = {
    .read = part_vnode_read, .write = part_vnode_write,
    .stat = part_vnode_stat, .ref = part_vnode_ref, .unref = part_vnode_unref,
};

static vnode_t g_part_vnodes[MAX_PARTITIONS];
static uint64_t g_part_ino_base = 300;

static int part_read_sectors(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    partition_blkdev_t *p = (partition_blkdev_t *)dev->priv;
    if (!p || !p->parent) return -EIO;
    if (lba + count > p->count_sectors) return -EINVAL;
    return p->parent->ops->read_sectors(p->parent, p->offset_sectors + lba, count, buf);
}

static int part_write_sectors(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    partition_blkdev_t *p = (partition_blkdev_t *)dev->priv;
    if (!p || !p->parent) return -EIO;
    if (lba + count > p->count_sectors) return -EINVAL;
    return p->parent->ops->write_sectors(p->parent, p->offset_sectors + lba, count, buf);
}

static int part_flush(blkdev_t *dev) {
    partition_blkdev_t *p = (partition_blkdev_t *)dev->priv;
    if (!p || !p->parent) return -EIO;
    return p->parent->ops->flush ? p->parent->ops->flush(p->parent) : 0;
}

static const blkdev_ops_t part_blkdev_ops = {
    .read_sectors  = part_read_sectors,
    .write_sectors = part_write_sectors,
    .flush         = part_flush,
};

int partition_read_mbr(blkdev_t *disk, mbr_t *out) {
    if (!disk || !out) return -EINVAL;
    uint8_t sector[512];
    int r = disk->ops->read_sectors(disk, 0, 1, sector);
    if (r < 0) return r;
    memcpy(out, sector, 512);
    return 0;
}

int partition_write_mbr(blkdev_t *disk, const mbr_partition_t parts[4],
                        uint32_t disk_signature)
{
    if (!disk || !parts) return -EINVAL;
    uint8_t sector[512];
    int r = disk->ops->read_sectors(disk, 0, 1, sector);
    if (r < 0) return r;

    mbr_t *mbr = (mbr_t *)sector;
    mbr->disk_signature = disk_signature;
    mbr->reserved = 0;
    for (int i = 0; i < 4; i++) mbr->partitions[i] = parts[i];
    mbr->signature = MBR_SIGNATURE;

    return disk->ops->write_sectors(disk, 0, 1, sector);
}

static const char *part_type_name(uint8_t t) {
    switch (t) {
        case MBR_TYPE_EMPTY:     return "empty";
        case MBR_TYPE_FAT12:     return "FAT12";
        case MBR_TYPE_FAT16_S:   return "FAT16 <32M";
        case MBR_TYPE_FAT16:     return "FAT16";
        case MBR_TYPE_EXTENDED:  return "Extended";
        case MBR_TYPE_FAT32_CHS: return "FAT32 CHS";
        case MBR_TYPE_FAT32_LBA: return "FAT32 LBA";
        case MBR_TYPE_FAT16_LBA: return "FAT16 LBA";
        case MBR_TYPE_LINUX:     return "Linux";
        case MBR_TYPE_ESP:       return "EFI System";
        default:                 return "unknown";
    }
}

int partition_scan(blkdev_t *disk) {
    if (!disk || !disk->present) return -ENODEV;

    mbr_t mbr;
    int r = partition_read_mbr(disk, &mbr);
    if (r < 0) {
        serial_printf("[part] %s: cannot read MBR: %d\n", disk->name, r);
        return r;
    }

    if (mbr.signature != MBR_SIGNATURE) {
        serial_printf("[part] %s: no MBR signature (raw disk / unpartitioned)\n",
                      disk->name);
        return 0;
    }

    int found = 0;
    for (int i = 0; i < 4; i++) {
        mbr_partition_t *p = &mbr.partitions[i];
        if (p->type == 0 || p->sector_count == 0) continue;
        if (g_partition_count >= MAX_PARTITIONS) break;

        partition_blkdev_t *pb = &g_partitions[g_partition_count];
        memset(pb, 0, sizeof(*pb));

        pb->parent         = disk;
        pb->offset_sectors = p->lba_start;
        pb->count_sectors  = p->sector_count;
        pb->type           = p->type;
        pb->bootable       = (p->boot_flag == 0x80) ? 1 : 0;
        pb->partnum        = (uint32_t)(i + 1);

        snprintf(pb->base.name, BLKDEV_NAME_MAX, "%s%u", disk->name, pb->partnum);
        pb->base.present      = true;
        pb->base.sector_count = pb->count_sectors;
        pb->base.size_bytes   = pb->count_sectors * (uint64_t)disk->sector_size;
        pb->base.sector_size  = disk->sector_size;
        pb->base.ops          = &part_blkdev_ops;
        pb->base.priv         = pb;

        blkdev_register(&pb->base);

        vnode_t *vn = &g_part_vnodes[g_partition_count];
        memset(vn, 0, sizeof(*vn));
        vn->type     = VFS_NODE_BLKDEV;
        vn->mode     = 0660;
        vn->ino      = g_part_ino_base + (uint64_t)g_partition_count;
        vn->ops      = &part_vnode_ops;
        vn->fs_data  = &pb->base;
        vn->size     = pb->base.size_bytes;
        vn->refcount = 1;
        devfs_register(pb->base.name, vn);

        serial_printf("[part] /dev/%s: type=0x%02x (%s) lba=%u sectors=%u %s\n",
                      pb->base.name, pb->type, part_type_name(pb->type),
                      p->lba_start, p->sector_count,
                      pb->bootable ? "[bootable]" : "");
        printf("[part] /dev/%s: 0x%02x (%s) %u MB %s\n",
               pb->base.name, pb->type, part_type_name(pb->type),
               (unsigned)(pb->base.size_bytes / (1024 * 1024)),
               pb->bootable ? "*" : "");

        g_partition_count++;
        found++;
    }

    if (found == 0) {
        serial_printf("[part] %s: MBR present but no valid partitions\n", disk->name);
    }
    return found;
}

blkdev_t *partition_get(const char *name) {
    for (int i = 0; i < g_partition_count; i++) {
        if (strcmp(g_partitions[i].base.name, name) == 0) return &g_partitions[i].base;
    }
    return NULL;
}