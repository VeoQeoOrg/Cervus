#include "../../include/fs/udf.h"
#include "../../include/fs/vfs.h"
#include "../../include/drivers/disk/blkdev.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define UDF_SECTOR 2048

#define TAG_AVDP 2
#define TAG_PD   5
#define TAG_LVD  6
#define TAG_TD   8
#define TAG_FSD  256
#define TAG_FID  257
#define TAG_FE   261
#define TAG_EFE  266

typedef struct {
    blkdev_t *dev;
    uint32_t  block_size;
    uint32_t  part_start;
    uint32_t  part_len;
} udf_fs_t;

typedef struct {
    udf_fs_t *fs;
    uint32_t  fe_lba;
    uint64_t  size;
    bool      is_dir;
} udf_node_t;

static const vnode_ops_t udf_file_ops;
static const vnode_ops_t udf_dir_ops;
static uint64_t g_udf_ino = 7000;

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t rd64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static int udf_read_sector(blkdev_t *dev, uint32_t lba, void *buf) {
    return dev->ops->read_sectors(dev, lba, 1, buf);
}

static void udf_fe_info(const uint8_t *fe, bool *is_dir, uint64_t *size) {
    *is_dir = (fe[27] == 4);
    *size   = rd64(fe + 56);
}

/* Copy [offset, offset+len) of the file described by the File Entry at fe_lba. */
static int64_t udf_read_range(udf_fs_t *fs, uint32_t fe_lba,
                              uint8_t *out, uint64_t len, uint64_t offset)
{
    uint8_t *fe = kmalloc(fs->block_size);
    if (!fe) return -ENOMEM;
    if (udf_read_sector(fs->dev, fe_lba, fe) < 0) { kfree(fe); return -EIO; }

    uint16_t tag = rd16(fe);
    int      efe = (tag == TAG_EFE);
    uint64_t info_len = rd64(fe + 56);
    uint32_t l_ea = efe ? rd32(fe + 208) : rd32(fe + 168);
    uint32_t l_ad = efe ? rd32(fe + 212) : rd32(fe + 172);
    uint32_t ad_off = (efe ? 216 : 176) + l_ea;
    int ad_type = fe[34] & 0x07;

    if (offset >= info_len) { kfree(fe); return 0; }
    if (len > info_len - offset) len = info_len - offset;

    uint64_t copied = 0;

    if (ad_type == 3) {
        uint64_t avail = l_ad;
        if (offset < avail) {
            uint64_t n = avail - offset;
            if (n > len) n = len;
            memcpy(out, fe + ad_off + offset, (size_t)n);
            copied = n;
        }
        kfree(fe);
        return (int64_t)copied;
    }

    uint8_t *tmp = kmalloc(fs->block_size);
    if (!tmp) { kfree(fe); return -ENOMEM; }

    uint64_t logical_pos = 0;
    uint32_t p = ad_off;
    uint32_t ad_end = ad_off + l_ad;
    uint32_t step = (ad_type == 1) ? 16 : 8;

    while (p + step <= ad_end && p + step <= fs->block_size && copied < len) {
        uint32_t raw = rd32(fe + p);
        uint32_t ext_len = raw & 0x3FFFFFFF;
        uint32_t et = raw >> 30;
        uint32_t ext_block = rd32(fe + p + 4);
        p += step;
        if (ext_len == 0) break;
        if (et >= 2) { logical_pos += ext_len; continue; }

        uint64_t ext_start = logical_pos;
        uint64_t ext_end = logical_pos + ext_len;
        uint64_t want = offset + copied;
        if (want < ext_end && want >= ext_start) {
            uint64_t in_ext = want - ext_start;
            uint32_t phys = fs->part_start + ext_block;
            while (copied < len && in_ext < ext_len) {
                uint32_t sidx = (uint32_t)(in_ext / fs->block_size);
                uint32_t soff = (uint32_t)(in_ext % fs->block_size);
                if (udf_read_sector(fs->dev, phys + sidx, tmp) < 0) {
                    kfree(tmp); kfree(fe); return (int64_t)copied;
                }
                uint32_t can = fs->block_size - soff;
                uint64_t rem_ext = ext_len - in_ext;
                if (can > rem_ext) can = (uint32_t)rem_ext;
                if (can > len - copied) can = (uint32_t)(len - copied);
                memcpy(out + copied, tmp + soff, can);
                copied += can;
                in_ext += can;
            }
        }
        logical_pos = ext_end;
    }

    kfree(tmp);
    kfree(fe);
    return (int64_t)copied;
}

static int udf_fid_name(const uint8_t *data, uint32_t name_start, uint8_t l_fi,
                        char *out, size_t cap)
{
    int nl = 0;
    if (l_fi > 0) {
        uint8_t comp = data[name_start];
        if (comp == 8) {
            for (int i = 1; i < l_fi && nl < (int)cap - 1; i++)
                out[nl++] = (char)data[name_start + i];
        } else if (comp == 16) {
            for (int i = 1; i + 1 < l_fi && nl < (int)cap - 1; i += 2)
                out[nl++] = (char)data[name_start + i + 1];
        }
    }
    out[nl] = '\0';
    return nl;
}

static int udf_scan_dir(udf_fs_t *fs, uint32_t dir_fe_lba, uint64_t dir_size,
                        const char *target, uint64_t want_index,
                        uint32_t *out_fe_lba, uint64_t *out_size, bool *out_is_dir,
                        char *out_name, size_t name_cap)
{
    if (dir_size == 0 || dir_size > 4u * 1024 * 1024) return -ENOENT;
    uint8_t *data = kmalloc((size_t)dir_size);
    if (!data) return -ENOMEM;

    int64_t got = udf_read_range(fs, dir_fe_lba, data, dir_size, 0);
    if (got < 0) { kfree(data); return -EIO; }

    uint64_t seen = 0;
    uint32_t off = 0;
    int rc = -ENOENT;

    while (off + 38 <= (uint32_t)got) {
        if (rd16(data + off) != TAG_FID) break;
        uint8_t  chars = data[off + 18];
        uint8_t  l_fi  = data[off + 19];
        uint32_t child_block = rd32(data + off + 24);
        uint16_t l_iu  = rd16(data + off + 36);
        uint32_t name_start = off + 38 + l_iu;
        uint32_t fid_len = (38u + l_iu + l_fi + 3u) & ~3u;
        if (name_start + l_fi > (uint32_t)got) break;

        if (chars & 0x0C) { off += fid_len; continue; }  /* parent or deleted */

        char nm[256];
        udf_fid_name(data, name_start, l_fi, nm, sizeof(nm));

        if (target) {
            if (strcmp(nm, target) == 0) {
                uint32_t fe = fs->part_start + child_block;
                uint8_t *cfe = kmalloc(fs->block_size);
                if (cfe && udf_read_sector(fs->dev, fe, cfe) == 0) {
                    udf_fe_info(cfe, out_is_dir, out_size);
                    *out_fe_lba = fe;
                    rc = 0;
                }
                if (cfe) kfree(cfe);
                break;
            }
        } else if (seen == want_index) {
            uint32_t fe = fs->part_start + child_block;
            *out_fe_lba = fe;
            *out_is_dir = (chars & 0x02) != 0;
            *out_size = 0;
            uint8_t *cfe = kmalloc(fs->block_size);
            if (cfe && udf_read_sector(fs->dev, fe, cfe) == 0)
                udf_fe_info(cfe, out_is_dir, out_size);
            if (cfe) kfree(cfe);
            if (out_name) { strncpy(out_name, nm, name_cap - 1); out_name[name_cap - 1] = 0; }
            rc = 0;
            break;
        } else {
            seen++;
        }
        off += fid_len;
    }

    kfree(data);
    return rc;
}

static void udf_ref(vnode_t *n) {
    if (n) __atomic_add_fetch(&n->refcount, 1, __ATOMIC_SEQ_CST);
}
static void udf_unref(vnode_t *n) {
    if (!n) return;
    if (__atomic_sub_fetch(&n->refcount, 1, __ATOMIC_SEQ_CST) <= 0) {
        if (n->fs_data) free(n->fs_data);
        free(n);
    }
}

static vnode_t *udf_alloc_vnode(udf_fs_t *fs, uint32_t fe_lba, uint64_t size, bool is_dir) {
    vnode_t *vn = calloc(1, sizeof(vnode_t));
    if (!vn) return NULL;
    udf_node_t *nd = calloc(1, sizeof(udf_node_t));
    if (!nd) { free(vn); return NULL; }
    nd->fs = fs; nd->fe_lba = fe_lba; nd->size = size; nd->is_dir = is_dir;
    vn->ino = g_udf_ino++;
    vn->refcount = 1;
    vn->fs_data = nd;
    vn->size = size;
    if (is_dir) { vn->type = VFS_NODE_DIR; vn->mode = 0555; vn->ops = &udf_dir_ops; }
    else        { vn->type = VFS_NODE_FILE; vn->mode = 0444; vn->ops = &udf_file_ops; }
    return vn;
}

static int udf_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    if (!dir || !name || !out) return -EINVAL;
    udf_node_t *nd = dir->fs_data;
    if (!nd || !nd->is_dir) return -ENOTDIR;
    uint32_t fe_lba = 0; uint64_t size = 0; bool is_dir = false;
    int r = udf_scan_dir(nd->fs, nd->fe_lba, nd->size, name, 0, &fe_lba, &size, &is_dir, NULL, 0);
    if (r < 0) return r;
    vnode_t *child = udf_alloc_vnode(nd->fs, fe_lba, size, is_dir);
    if (!child) return -ENOMEM;
    *out = child;
    return 0;
}

static int udf_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    if (!dir || !out) return -EINVAL;
    udf_node_t *nd = dir->fs_data;
    if (!nd || !nd->is_dir) return -ENOTDIR;
    uint32_t fe_lba = 0; uint64_t size = 0; bool is_dir = false;
    char nm[VFS_MAX_NAME];
    int r = udf_scan_dir(nd->fs, nd->fe_lba, nd->size, NULL, index,
                         &fe_lba, &size, &is_dir, nm, sizeof(nm));
    if (r < 0) return r;
    memset(out, 0, sizeof(*out));
    strncpy(out->d_name, nm, VFS_MAX_NAME - 1);
    out->d_type = is_dir ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->d_ino = fe_lba;
    return 0;
}

static int udf_stat(vnode_t *n, vfs_stat_t *out) {
    if (!n || !out) return -EINVAL;
    udf_node_t *nd = n->fs_data;
    memset(out, 0, sizeof(*out));
    out->st_ino = n->ino;
    out->st_type = n->type;
    out->st_mode = n->mode;
    out->st_size = nd ? nd->size : 0;
    out->st_blocks = nd ? (nd->size + 511) / 512 : 0;
    return 0;
}

static int64_t udf_file_read(vnode_t *n, void *buf, size_t len, uint64_t offset) {
    if (!n || !buf) return -EINVAL;
    udf_node_t *nd = n->fs_data;
    if (!nd || nd->is_dir) return -EISDIR;
    return udf_read_range(nd->fs, nd->fe_lba, buf, len, offset);
}

static const vnode_ops_t udf_file_ops = {
    .read  = udf_file_read,
    .stat  = udf_stat,
    .ref   = udf_ref,
    .unref = udf_unref,
};
static const vnode_ops_t udf_dir_ops = {
    .lookup  = udf_lookup,
    .readdir = udf_readdir,
    .stat    = udf_stat,
    .ref     = udf_ref,
    .unref   = udf_unref,
};

int udf_detect(blkdev_t *dev) {
    if (!dev) return 0;
    uint8_t *sec = kmalloc(UDF_SECTOR);
    if (!sec) return 0;
    int found = 0;
    for (uint32_t s = 16; s < 32; s++) {
        if (udf_read_sector(dev, s, sec) < 0) break;
        if (memcmp(sec + 1, "NSR02", 5) == 0 || memcmp(sec + 1, "NSR03", 5) == 0) { found = 1; break; }
        if (memcmp(sec + 1, "TEA01", 5) == 0) break;
        if (sec[0] == 0 &&
            memcmp(sec + 1, "BEA01", 5) != 0 &&
            memcmp(sec + 1, "CD001", 5) != 0 &&
            memcmp(sec + 1, "BOOT2", 5) != 0) break;
    }
    kfree(sec);
    return found;
}

vnode_t *udf_mount(blkdev_t *dev) {
    if (!dev) return NULL;
    uint8_t *sec = kmalloc(UDF_SECTOR);
    if (!sec) return NULL;

    if (udf_read_sector(dev, 256, sec) < 0 || rd16(sec) != TAG_AVDP) {
        serial_printf("[udf] %s: no anchor at sector 256\n", dev->name);
        kfree(sec);
        return NULL;
    }
    uint32_t mvds_len = rd32(sec + 16);
    uint32_t mvds_loc = rd32(sec + 20);

    uint32_t part_start = 0, part_len = 0, block_size = UDF_SECTOR;
    uint32_t fsd_len = 0, fsd_block = 0;

    uint32_t nsec = mvds_len / UDF_SECTOR;
    if (nsec > 64) nsec = 64;
    for (uint32_t i = 0; i < nsec; i++) {
        if (udf_read_sector(dev, mvds_loc + i, sec) < 0) break;
        uint16_t tag = rd16(sec);
        if (tag == 0) continue;
        if (tag == TAG_PD) {
            part_start = rd32(sec + 188);
            part_len   = rd32(sec + 192);
        } else if (tag == TAG_LVD) {
            block_size = rd32(sec + 212);
            fsd_len    = rd32(sec + 248);
            fsd_block  = rd32(sec + 248 + 4);
        } else if (tag == TAG_TD) {
            break;
        }
    }

    if (!part_len || !fsd_len) {
        serial_printf("[udf] %s: incomplete volume descriptors (part=%u fsd=%u)\n",
                      dev->name, part_len, fsd_len);
        kfree(sec);
        return NULL;
    }
    if (block_size != UDF_SECTOR) {
        serial_printf("[udf] %s: unsupported block size %u (need 2048)\n", dev->name, block_size);
        kfree(sec);
        return NULL;
    }

    if (udf_read_sector(dev, part_start + fsd_block, sec) < 0 || rd16(sec) != TAG_FSD) {
        serial_printf("[udf] %s: no file set descriptor\n", dev->name);
        kfree(sec);
        return NULL;
    }
    uint32_t root_block = rd32(sec + 400 + 4);
    uint32_t root_fe = part_start + root_block;

    udf_fs_t *fs = calloc(1, sizeof(udf_fs_t));
    if (!fs) { kfree(sec); return NULL; }
    fs->dev = dev;
    fs->block_size = block_size;
    fs->part_start = part_start;
    fs->part_len = part_len;

    if (udf_read_sector(dev, root_fe, sec) < 0) { free(fs); kfree(sec); return NULL; }
    bool is_dir; uint64_t size;
    udf_fe_info(sec, &is_dir, &size);

    serial_printf("[udf] %s: mounted (part_start=%u part_len=%u root_fe=%u size=%llu)\n",
                  dev->name, part_start, part_len, root_fe, (unsigned long long)size);

    kfree(sec);
    vnode_t *root = udf_alloc_vnode(fs, root_fe, size, true);
    if (!root) { free(fs); return NULL; }
    return root;
}
