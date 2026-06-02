#include "../../../include/drivers/disk/partition.h"
#include "../../../include/drivers/disk/blkdev.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/io/serial.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/memory/pmm.h"
#include <string.h>
#include <stdio.h>

#define MAX_PARTITIONS 32

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

typedef struct { uint8_t guid[16]; const char *name; } gpt_known_t;

static const gpt_known_t g_gpt_known[] = {
    { { 0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B },
      "EFI System" },
    { { 0xAF,0x3D,0xC6,0x0F, 0x83,0x84, 0x72,0x47, 0x8E,0x79, 0x3D,0x69,0xD8,0x47,0x7D,0xE4 },
      "Linux filesystem" },
    { { 0x6D,0xFD,0x57,0x06, 0xAB,0xA4, 0xC4,0x43, 0x84,0xE5, 0x09,0x33,0xC8,0x4B,0x4F,0x4F },
      "Linux swap" },
    { { 0x48,0x61,0x68,0x21, 0x49,0x64, 0x6F,0x6E, 0x74,0x4E, 0x65,0x65,0x64,0x45,0x46,0x49 },
      "BIOS boot" },
    { { 0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44, 0x87,0xC0, 0x68,0xB6,0xB7,0x26,0x99,0xC7 },
      "Microsoft basic data" },
    { { 0x79,0xD3,0xD6,0xE6, 0x07,0xF5, 0xC2,0x44, 0xA2,0x3C, 0x23,0x8F,0x2A,0x3D,0xF9,0x28 },
      "Linux LVM" },
    { { 0xAA,0xC8,0x08,0x85, 0x8F,0x7E, 0xE0,0x42, 0x85,0xD2, 0xE1,0xE9,0x04,0x34,0xCF,0xB3 },
      "Microsoft reserved" },
    { { 0xA4,0xBB,0x94,0xDE, 0xD1,0x06, 0x40,0x4D, 0xA1,0x6A, 0xBF,0xD5,0x01,0x79,0xD6,0xAC },
      "Apple HFS+" },
};

const char *gpt_type_guid_name(const uint8_t guid[16]) {
    for (size_t i = 0; i < sizeof(g_gpt_known) / sizeof(g_gpt_known[0]); i++) {
        if (memcmp(guid, g_gpt_known[i].guid, 16) == 0) return g_gpt_known[i].name;
    }
    return "GPT partition";
}

static bool guid_is_zero(const uint8_t guid[16]) {
    for (int i = 0; i < 16; i++) if (guid[i]) return false;
    return true;
}

static int partition_scan_gpt(blkdev_t *disk) {
    uint32_t sec_size = disk->sector_size ? disk->sector_size : 512;
    uint8_t hdr_sec[512];

    int r = disk->ops->read_sectors(disk, 1, 1, hdr_sec);
    if (r < 0) {
        serial_printf("[part] %s: cannot read GPT header: %d\n", disk->name, r);
        return r;
    }

    gpt_header_t hdr;
    memcpy(&hdr, hdr_sec, sizeof(hdr));

    static const uint8_t gpt_sig[8] = GPT_SIGNATURE_BYTES;
    if (memcmp(hdr.signature, gpt_sig, 8) != 0) {
        serial_printf("[part] %s: GPT signature missing (protective MBR but no GPT header)\n",
                      disk->name);
        return 0;
    }
    if (hdr.entry_size < sizeof(gpt_entry_t) || hdr.entry_size > 4096) {
        serial_printf("[part] %s: GPT entry_size=%u out of range\n",
                      disk->name, hdr.entry_size);
        return -EINVAL;
    }
    if (hdr.entry_count == 0 || hdr.entry_count > 512) {
        serial_printf("[part] %s: GPT entry_count=%u out of range\n",
                      disk->name, hdr.entry_count);
        return -EINVAL;
    }

    uint64_t arr_bytes   = (uint64_t)hdr.entry_count * hdr.entry_size;
    uint32_t arr_sectors = (uint32_t)((arr_bytes + sec_size - 1) / sec_size);

    uint8_t *arr_buf = kmalloc((size_t)arr_sectors * sec_size);
    if (!arr_buf) return -ENOMEM;

    r = disk->ops->read_sectors(disk, hdr.entry_array_lba, arr_sectors, arr_buf);
    if (r < 0) {
        serial_printf("[part] %s: cannot read GPT entry array (lba=%llu, %u sec): %d\n",
                      disk->name, (unsigned long long)hdr.entry_array_lba, arr_sectors, r);
        kfree(arr_buf);
        return r;
    }

    int found = 0;
    for (uint32_t i = 0; i < hdr.entry_count; i++) {
        gpt_entry_t *e = (gpt_entry_t *)(arr_buf + (size_t)i * hdr.entry_size);
        if (guid_is_zero(e->type_guid)) continue;
        if (e->last_lba < e->first_lba) continue;

        int slot = -1;
        for (int j = 0; j < g_partition_count; j++) {
            if (g_partitions[j].parent == disk &&
                g_partitions[j].partnum == i + 1) {
                slot = j;
                break;
            }
        }
        bool is_new = (slot < 0);
        if (is_new) {
            if (g_partition_count >= MAX_PARTITIONS) {
                serial_printf("[part] %s: MAX_PARTITIONS reached, stopping at %u\n",
                              disk->name, i);
                break;
            }
            slot = g_partition_count;
        }
        partition_blkdev_t *pb = &g_partitions[slot];
        memset(pb, 0, sizeof(*pb));

        pb->parent         = disk;
        pb->offset_sectors = e->first_lba;
        pb->count_sectors  = e->last_lba - e->first_lba + 1;
        pb->type           = 0;
        pb->bootable       = 0;
        pb->partnum        = i + 1;

        {
            size_t nlen = strlen(disk->name);
            const char *sep = "";
            if (nlen > 0 && disk->name[nlen - 1] >= '0' && disk->name[nlen - 1] <= '9') {
                sep = "p";
            }
            snprintf(pb->base.name, BLKDEV_NAME_MAX, "%s%s%u",
                     disk->name, sep, pb->partnum);
        }
        pb->base.present      = true;
        pb->base.is_partition = true;
        pb->base.sector_count = pb->count_sectors;
        pb->base.size_bytes   = pb->count_sectors * (uint64_t)sec_size;
        pb->base.sector_size  = sec_size;
        pb->base.ops          = &part_blkdev_ops;
        pb->base.priv         = pb;
        pb->base.part_lba_start = pb->offset_sectors;
        pb->base.part_type      = 0xEE;
        pb->base.part_bootable  = 0;
        strncpy(pb->base.model, disk->model, BLKDEV_MODEL_MAX - 1);

        vnode_t *vn = &g_part_vnodes[slot];
        memset(vn, 0, sizeof(*vn));
        vn->type     = VFS_NODE_BLKDEV;
        vn->mode     = 0660;
        vn->ino      = g_part_ino_base + (uint64_t)slot;
        vn->ops      = &part_vnode_ops;
        vn->fs_data  = &pb->base;
        vn->size     = pb->base.size_bytes;
        vn->refcount = 1;

        if (is_new) {
            blkdev_register(&pb->base);
            g_partition_count++;
        }
        devfs_register(pb->base.name, vn);

        const char *type_name = gpt_type_guid_name(e->type_guid);
        serial_printf("[part] /dev/%s: GPT %s lba=%llu..%llu (%llu MB)\n",
                      pb->base.name, type_name,
                      (unsigned long long)e->first_lba,
                      (unsigned long long)e->last_lba,
                      (unsigned long long)(pb->base.size_bytes / (1024 * 1024)));

        found++;
    }

    kfree(arr_buf);

    if (found == 0)
        serial_printf("[part] %s: GPT present but no usable entries\n", disk->name);

    return found;
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

    for (int i = 0; i < 4; i++) {
        if (mbr.partitions[i].type == MBR_TYPE_GPT_PROT) {
            serial_printf("[part] %s: protective MBR detected, switching to GPT\n",
                          disk->name);
            return partition_scan_gpt(disk);
        }
    }

    int found = 0;
    for (int i = 0; i < 4; i++) {
        mbr_partition_t *p = &mbr.partitions[i];
        if (p->type == 0 || p->sector_count == 0) continue;

        int slot = -1;
        for (int j = 0; j < g_partition_count; j++) {
            if (g_partitions[j].parent == disk &&
                g_partitions[j].partnum == (uint32_t)(i + 1)) {
                slot = j;
                break;
            }
        }
        bool is_new = (slot < 0);
        if (is_new) {
            if (g_partition_count >= MAX_PARTITIONS) break;
            slot = g_partition_count;
        }
        partition_blkdev_t *pb = &g_partitions[slot];
        memset(pb, 0, sizeof(*pb));

        pb->parent         = disk;
        pb->offset_sectors = p->lba_start;
        pb->count_sectors  = p->sector_count;
        pb->type           = p->type;
        pb->bootable       = (p->boot_flag == 0x80) ? 1 : 0;
        pb->partnum        = (uint32_t)(i + 1);

        {
            size_t nlen = strlen(disk->name);
            const char *sep = "";
            if (nlen > 0 && disk->name[nlen - 1] >= '0' && disk->name[nlen - 1] <= '9') {
                sep = "p";
            }
            snprintf(pb->base.name, BLKDEV_NAME_MAX, "%s%s%u",
                     disk->name, sep, pb->partnum);
        }
        pb->base.present      = true;
        pb->base.is_partition = true;
        pb->base.sector_count = pb->count_sectors;
        pb->base.size_bytes   = pb->count_sectors * (uint64_t)disk->sector_size;
        pb->base.sector_size  = disk->sector_size;
        pb->base.ops          = &part_blkdev_ops;
        pb->base.priv         = pb;
        pb->base.part_lba_start = pb->offset_sectors;
        pb->base.part_type      = pb->type;
        pb->base.part_bootable  = pb->bootable;
        strncpy(pb->base.model, disk->model, BLKDEV_MODEL_MAX - 1);

        vnode_t *vn = &g_part_vnodes[slot];
        memset(vn, 0, sizeof(*vn));
        vn->type     = VFS_NODE_BLKDEV;
        vn->mode     = 0660;
        vn->ino      = g_part_ino_base + (uint64_t)slot;
        vn->ops      = &part_vnode_ops;
        vn->fs_data  = &pb->base;
        vn->size     = pb->base.size_bytes;
        vn->refcount = 1;

        if (is_new) {
            blkdev_register(&pb->base);
            g_partition_count++;
        }
        devfs_register(pb->base.name, vn);

        serial_printf("[part] /dev/%s: type=0x%02x (%s) lba=%u sectors=%u %s\n",
                      pb->base.name, pb->type, part_type_name(pb->type),
                      p->lba_start, p->sector_count,
                      pb->bootable ? "[bootable]" : "");
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

int partition_remove_children(blkdev_t *parent) {
    int n = 0;
    for (int i = 0; i < g_partition_count; i++) {
        partition_blkdev_t *pb = &g_partitions[i];
        if (pb->parent == parent && pb->base.present) {
            pb->base.present = false;
            serial_printf("[part] hiding %s (parent %s removed)\n",
                          pb->base.name, parent->name);
            n++;
        }
    }
    return n;
}

static uint32_t crc32_table[256];
static bool     crc32_table_inited = false;

static void crc32_init_table(void) {
    if (crc32_table_inited) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_inited = true;
}

static uint32_t crc32_compute(const void *data, size_t len) {
    crc32_init_table();
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) crc = crc32_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static uint64_t prng_state = 0xcbf29ce484222325ULL;
static uint64_t prng_next(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 7;
    prng_state ^= prng_state << 17;
    return prng_state;
}

static void random_guid(uint8_t out[16]) {
    uint64_t a = prng_next();
    uint64_t b = prng_next();
    for (int i = 0; i < 8; i++) { out[i]     = (uint8_t)(a >> (i * 8)); }
    for (int i = 0; i < 8; i++) { out[8 + i] = (uint8_t)(b >> (i * 8)); }
    out[7] = (out[7] & 0x0F) | 0x40;
    out[8] = (out[8] & 0x3F) | 0x80;
}

int partition_write_gpt(blkdev_t *disk,
                        const gpt_partition_spec_t *specs, size_t count,
                        const uint8_t disk_guid_in[16])
{
    if (!disk || !disk->present) return -ENODEV;
    if (count == 0 || count > 128) return -EINVAL;
    if (disk->sector_size != 512) return -EINVAL;

    uint64_t total = disk->sector_count;
    if (total < 67) return -EINVAL;

    const uint32_t entry_count = 128;
    const uint32_t entry_size  = 128;
    const uint32_t arr_bytes   = entry_count * entry_size;
    const uint32_t arr_sectors = (arr_bytes + 511) / 512;

    uint8_t *arr = kmalloc(arr_bytes);
    if (!arr) return -ENOMEM;
    memset(arr, 0, arr_bytes);

    for (size_t i = 0; i < count; i++) {
        gpt_entry_t *e = (gpt_entry_t *)(arr + i * entry_size);
        memcpy(e->type_guid, specs[i].type_guid, 16);
        random_guid(e->part_guid);
        e->first_lba  = specs[i].first_lba;
        e->last_lba   = specs[i].last_lba;
        e->attributes = 0;
        for (int k = 0; k < 36; k++) {
            char c = specs[i].name[k];
            if (!c) break;
            e->name_utf16[k] = (uint16_t)(uint8_t)c;
        }
    }

    uint32_t arr_crc = crc32_compute(arr, arr_bytes);

    uint8_t mbr_sec[512] = {0};
    mbr_t *mbr = (mbr_t *)mbr_sec;
    mbr->disk_signature = 0;
    mbr->reserved       = 0;
    mbr->partitions[0].boot_flag    = 0;
    mbr->partitions[0].chs_start[0] = 0x00;
    mbr->partitions[0].chs_start[1] = 0x02;
    mbr->partitions[0].chs_start[2] = 0x00;
    mbr->partitions[0].type         = MBR_TYPE_GPT_PROT;
    mbr->partitions[0].chs_end[0]   = 0xFE;
    mbr->partitions[0].chs_end[1]   = 0xFF;
    mbr->partitions[0].chs_end[2]   = 0xFF;
    mbr->partitions[0].lba_start    = 1;
    mbr->partitions[0].sector_count =
        total > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)(total - 1);
    mbr->signature = MBR_SIGNATURE;

    if (disk->ops->write_sectors(disk, 0, 1, mbr_sec) < 0) {
        kfree(arr);
        return -EIO;
    }

    uint64_t primary_arr_lba = 2;
    uint64_t backup_hdr_lba  = total - 1;
    uint64_t backup_arr_lba  = total - 1 - arr_sectors;
    uint64_t first_usable    = primary_arr_lba + arr_sectors;
    uint64_t last_usable     = backup_arr_lba - 1;

    gpt_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    static const uint8_t sig[8] = GPT_SIGNATURE_BYTES;
    memcpy(hdr.signature, sig, 8);
    hdr.revision          = 0x00010000;
    hdr.header_size       = 92;
    hdr.header_crc32      = 0;
    hdr.reserved          = 0;
    hdr.my_lba            = 1;
    hdr.alternate_lba     = backup_hdr_lba;
    hdr.first_usable_lba  = first_usable;
    hdr.last_usable_lba   = last_usable;
    if (disk_guid_in) memcpy(hdr.disk_guid, disk_guid_in, 16);
    else              random_guid(hdr.disk_guid);
    hdr.entry_array_lba   = primary_arr_lba;
    hdr.entry_count       = entry_count;
    hdr.entry_size        = entry_size;
    hdr.entry_array_crc32 = arr_crc;
    hdr.header_crc32      = crc32_compute(&hdr, 92);

    uint8_t hdr_sec[512] = {0};
    memcpy(hdr_sec, &hdr, sizeof(hdr));
    if (disk->ops->write_sectors(disk, 1, 1, hdr_sec) < 0) {
        kfree(arr);
        return -EIO;
    }

    if (disk->ops->write_sectors(disk, primary_arr_lba, arr_sectors, arr) < 0) {
        kfree(arr);
        return -EIO;
    }

    if (disk->ops->write_sectors(disk, backup_arr_lba, arr_sectors, arr) < 0) {
        kfree(arr);
        return -EIO;
    }

    gpt_header_t bhdr = hdr;
    bhdr.my_lba          = backup_hdr_lba;
    bhdr.alternate_lba   = 1;
    bhdr.entry_array_lba = backup_arr_lba;
    bhdr.header_crc32    = 0;
    bhdr.header_crc32    = crc32_compute(&bhdr, 92);

    uint8_t bhdr_sec[512] = {0};
    memcpy(bhdr_sec, &bhdr, sizeof(bhdr));
    if (disk->ops->write_sectors(disk, backup_hdr_lba, 1, bhdr_sec) < 0) {
        kfree(arr);
        return -EIO;
    }

    if (disk->ops->flush) disk->ops->flush(disk);

    kfree(arr);
    return 0;
}