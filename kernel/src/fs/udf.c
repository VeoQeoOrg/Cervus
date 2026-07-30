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
#define TAG_LVID 9
#define TAG_FSD  256
#define TAG_FID  257
#define TAG_SBD  264
#define TAG_FE   261
#define TAG_EFE  266

typedef struct {
    blkdev_t *dev;
    uint32_t  block_size;
    uint32_t  part_start;
    uint32_t  part_len;
    uint32_t  bitmap_lba;
    uint32_t  bitmap_nbits;
    uint32_t  bitmap_nbytes;
    uint16_t  desc_ver;
    uint16_t  tag_serial;
    uint32_t  root_block;
    uint32_t  lvid_lba;
    uint32_t  lvid_nparts;
    uint32_t  free_blocks;
    uint64_t  next_uniqid;
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

static int udf_write_sector(blkdev_t *dev, uint32_t lba, const void *buf) {
    if (!dev->ops->write_sectors) return -EROFS;
    return dev->ops->write_sectors(dev, lba, 1, buf);
}

static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void wr64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) { p[i] = (uint8_t)v; v >>= 8; }
}

static uint16_t udf_crc(const uint8_t *d, size_t n) {
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static void udf_finalize_tag(udf_fs_t *fs, uint8_t *desc, uint16_t tag_id,
                             uint16_t crc_len, uint32_t tag_loc) {
    wr16(desc + 0, tag_id);
    wr16(desc + 2, fs->desc_ver);
    desc[4] = 0;
    desc[5] = 0;
    wr16(desc + 6, fs->tag_serial);
    wr16(desc + 8, udf_crc(desc + 16, crc_len));
    wr16(desc + 10, crc_len);
    wr32(desc + 12, tag_loc);
    uint8_t sum = 0;
    for (int i = 0; i < 16; i++) if (i != 4) sum += desc[i];
    desc[4] = sum;
}

static int udf_alloc_block(udf_fs_t *fs) {
    if (!fs->bitmap_lba || fs->bitmap_nbits == 0) return -1;
    uint32_t total = 24 + fs->bitmap_nbytes;
    uint32_t nsec = (total + fs->block_size - 1) / fs->block_size;
    uint8_t *buf = kmalloc(nsec * fs->block_size);
    if (!buf) return -1;
    for (uint32_t s = 0; s < nsec; s++)
        if (udf_read_sector(fs->dev, fs->bitmap_lba + s, buf + (size_t)s * fs->block_size) < 0) {
            kfree(buf); return -1;
        }
    int result = -1;
    for (uint32_t bit = 0; bit < fs->bitmap_nbits; bit++) {
        uint32_t bidx = 24 + bit / 8;
        uint8_t mask = (uint8_t)(1u << (bit & 7));
        if (buf[bidx] & mask) {
            buf[bidx] &= (uint8_t)~mask;
            uint32_t sec = bidx / fs->block_size;
            if (udf_write_sector(fs->dev, fs->bitmap_lba + sec,
                                 buf + (size_t)sec * fs->block_size) == 0) {
                result = (int)bit;
                if (fs->free_blocks) fs->free_blocks--;
            }
            break;
        }
    }
    kfree(buf);
    return result;
}

static void udf_free_block(udf_fs_t *fs, uint32_t block) {
    if (!fs->bitmap_lba || block >= fs->bitmap_nbits) return;
    uint32_t bidx = 24 + block / 8;
    uint32_t sec = bidx / fs->block_size;
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return;
    if (udf_read_sector(fs->dev, fs->bitmap_lba + sec, buf) == 0) {
        uint32_t o = bidx - sec * fs->block_size;
        uint8_t m = (uint8_t)(1u << (block & 7));
        if (!(buf[o] & m)) {
            buf[o] |= m;
            udf_write_sector(fs->dev, fs->bitmap_lba + sec, buf);
            fs->free_blocks++;
        }
    }
    kfree(buf);
}

static void udf_flush_lvid(udf_fs_t *fs) {
    if (!fs->lvid_lba) return;
    uint8_t *b = kmalloc(fs->block_size);
    if (!b) return;
    if (udf_read_sector(fs->dev, fs->lvid_lba, b) < 0 || rd16(b) != TAG_LVID) { kfree(b); return; }
    uint16_t crc_len = rd16(b + 10);
    wr32(b + 28, 1);
    wr64(b + 40, fs->next_uniqid);
    wr32(b + 80, fs->free_blocks);
    wr16(b + 8, udf_crc(b + 16, crc_len));
    b[4] = 0;
    uint8_t sum = 0;
    for (int i = 0; i < 16; i++) if (i != 4) sum += b[i];
    b[4] = sum;
    udf_write_sector(fs->dev, fs->lvid_lba, b);
    kfree(b);
}

static void udf_free_fe_extents(udf_fs_t *fs, const uint8_t *fe) {
    int efe = (rd16(fe) == TAG_EFE);
    if ((fe[34] & 7) != 0) return;
    uint32_t l_ea = efe ? rd32(fe + 208) : rd32(fe + 168);
    uint32_t l_ad = efe ? rd32(fe + 212) : rd32(fe + 172);
    uint32_t ad_area = (efe ? 216u : 176u) + l_ea;
    for (uint32_t p = 0; p + 8 <= l_ad; p += 8) {
        uint32_t raw = rd32(fe + ad_area + p);
        uint32_t et = raw >> 30;
        uint32_t blk = rd32(fe + ad_area + p + 4);
        uint32_t elen = raw & 0x3FFFFFFF;
        if (et >= 2 || elen == 0) continue;
        uint32_t nb = (elen + fs->block_size - 1) / fs->block_size;
        for (uint32_t k = 0; k < nb; k++) udf_free_block(fs, blk + k);
    }
}

static void udf_fe_info(const uint8_t *fe, bool *is_dir, uint64_t *size) {
    *is_dir = (fe[27] == 4);
    *size   = rd64(fe + 56);
}

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

static int udf_scan_dir(udf_fs_t *fs, uint32_t dir_fe_lba, uint64_t dir_size_hint,
                        const char *target, uint64_t want_index,
                        uint32_t *out_fe_lba, uint64_t *out_size, bool *out_is_dir,
                        char *out_name, size_t name_cap)
{
    (void)dir_size_hint;
    uint8_t *hdr = kmalloc(fs->block_size);
    if (!hdr) return -ENOMEM;
    if (udf_read_sector(fs->dev, dir_fe_lba, hdr) < 0) { kfree(hdr); return -EIO; }
    uint64_t dir_size = rd64(hdr + 56);
    kfree(hdr);

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

        if (chars & 0x0C) { off += fid_len; continue; }

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

static void udf_refinalize_fids(udf_fs_t *fs, uint8_t *data, uint32_t size,
                                const uint32_t *blocks, uint32_t bs, uint32_t inline_block) {
    uint32_t off = 0;
    while (off + 38 <= size) {
        uint8_t *f = data + off;
        if (rd16(f) != TAG_FID) break;
        uint8_t  l_fi = f[19];
        uint16_t l_iu = rd16(f + 36);
        uint32_t fid_len = (uint32_t)((38 + l_iu + l_fi + 3) & ~3);
        if (off + fid_len > size) break;
        uint32_t tag_loc = blocks ? blocks[off / bs] : inline_block;
        udf_finalize_tag(fs, f, TAG_FID, (uint16_t)(fid_len - 16), tag_loc);
        off += fid_len;
    }
}

static int udf_write_fe_data(udf_fs_t *fs, uint8_t *fe, uint32_t fe_lba,
                             uint8_t *data, uint64_t size, int is_dir) {
    uint32_t bs = fs->block_size;
    int efe = (rd16(fe) == TAG_EFE);
    uint32_t l_ea = efe ? rd32(fe + 208) : rd32(fe + 168);
    uint32_t ad_area = (efe ? 216u : 176u) + l_ea;
    uint32_t inline_cap = bs - ad_area;
    uint32_t tag_loc = fe_lba - fs->part_start;

    memset(fe + ad_area, 0, bs - ad_area);

    if (size <= inline_cap) {
        fe[34] = (uint8_t)((fe[34] & ~7) | 3);
        if (size) memcpy(fe + ad_area, data, (size_t)size);
        if (is_dir) udf_refinalize_fids(fs, fe + ad_area, (uint32_t)size, NULL, bs, tag_loc);
        wr64(fe + 56, size);
        if (efe) { wr64(fe + 64, size); wr64(fe + 72, 0); wr32(fe + 212, (uint32_t)size); }
        else     { wr64(fe + 64, 0);    wr32(fe + 172, (uint32_t)size); }
        udf_finalize_tag(fs, fe, efe ? TAG_EFE : TAG_FE, (uint16_t)(ad_area + size - 16), tag_loc);
        return udf_write_sector(fs->dev, fe_lba, fe);
    }

    if (size > 8u * 1024 * 1024) return -EFBIG;
    uint32_t nblocks = (uint32_t)((size + bs - 1) / bs);
    uint32_t max_ads = (bs - ad_area) / 8;
    uint32_t *blocks = kmalloc(nblocks * sizeof(uint32_t));
    if (!blocks) return -ENOMEM;
    uint32_t got = 0;
    for (; got < nblocks; got++) { int b = udf_alloc_block(fs); if (b < 0) break; blocks[got] = (uint32_t)b; }
    if (got < nblocks) {
        for (uint32_t k = 0; k < got; k++) udf_free_block(fs, blocks[k]);
        kfree(blocks);
        return -ENOSPC;
    }

    uint8_t *ad = fe + ad_area;
    uint32_t l_ad = 0, i = 0;
    int overflow = 0;
    while (i < nblocks) {
        if (l_ad / 8 >= max_ads) { overflow = 1; break; }
        uint32_t start = blocks[i], run = 1;
        while (i + run < nblocks && blocks[i + run] == start + run) run++;
        uint64_t eb = (i + run >= nblocks) ? (size - (uint64_t)i * bs) : (uint64_t)run * bs;
        wr32(ad + l_ad, (uint32_t)eb);
        wr32(ad + l_ad + 4, start);
        l_ad += 8;
        i += run;
    }
    if (overflow) {
        for (uint32_t k = 0; k < nblocks; k++) udf_free_block(fs, blocks[k]);
        kfree(blocks);
        return -EFBIG;
    }

    if (is_dir) udf_refinalize_fids(fs, data, (uint32_t)size, blocks, bs, tag_loc);

    uint8_t *sec = kmalloc(bs);
    for (uint32_t bi = 0; bi < nblocks; bi++) {
        memset(sec, 0, bs);
        uint64_t off = (uint64_t)bi * bs;
        uint32_t cpy = (uint32_t)((size - off > bs) ? bs : (size - off));
        memcpy(sec, data + off, cpy);
        udf_write_sector(fs->dev, fs->part_start + blocks[bi], sec);
    }
    kfree(sec);
    kfree(blocks);

    fe[34] = (uint8_t)(fe[34] & ~7);
    wr64(fe + 56, size);
    if (efe) { wr64(fe + 64, size); wr64(fe + 72, nblocks); wr32(fe + 212, l_ad); }
    else     { wr64(fe + 64, nblocks); wr32(fe + 172, l_ad); }
    udf_finalize_tag(fs, fe, efe ? TAG_EFE : TAG_FE, (uint16_t)(ad_area + l_ad - 16), tag_loc);
    return udf_write_sector(fs->dev, fe_lba, fe);
}

static int64_t udf_resize_write(vnode_t *n, const void *buf, size_t len, uint64_t offset, int is_trunc) {
    udf_node_t *nd = n->fs_data;
    if (!nd || nd->is_dir) return -EISDIR;
    udf_fs_t *fs = nd->fs;
    uint32_t bs = fs->block_size;

    uint8_t *fe = kmalloc(bs);
    if (!fe) return -ENOMEM;
    if (udf_read_sector(fs->dev, nd->fe_lba, fe) < 0) { kfree(fe); return -EIO; }
    uint64_t cur_size = rd64(fe + 56);

    uint64_t new_size = is_trunc ? offset : ((offset + len > cur_size) ? (offset + len) : cur_size);
    if (new_size > 8u * 1024 * 1024) { kfree(fe); return -EFBIG; }

    uint8_t *data = kmalloc(new_size ? (size_t)new_size : 1);
    if (!data) { kfree(fe); return -ENOMEM; }
    memset(data, 0, new_size ? (size_t)new_size : 1);
    uint64_t keep = (cur_size < new_size) ? cur_size : new_size;
    if (keep > 0) udf_read_range(fs, nd->fe_lba, data, keep, 0);
    if (!is_trunc && len > 0) memcpy(data + offset, buf, len);

    udf_free_fe_extents(fs, fe);
    int r = udf_write_fe_data(fs, fe, nd->fe_lba, data, new_size, 0);
    kfree(data); kfree(fe);
    if (r < 0) return r;
    nd->size = new_size; n->size = new_size;
    udf_flush_lvid(fs);
    return is_trunc ? 0 : (int64_t)len;
}

static int udf_load_blocks(udf_fs_t *fs, const uint8_t *fe, uint32_t **out_list) {
    int efe = (rd16(fe) == TAG_EFE);
    uint32_t l_ea = efe ? rd32(fe + 208) : rd32(fe + 168);
    uint32_t l_ad = efe ? rd32(fe + 212) : rd32(fe + 172);
    uint32_t ad_area = (efe ? 216u : 176u) + l_ea;
    uint32_t bs = fs->block_size;
    uint32_t total = 0;
    for (uint32_t p = 0; p + 8 <= l_ad; p += 8) {
        uint32_t raw = rd32(fe + ad_area + p);
        uint32_t et = raw >> 30, elen = raw & 0x3FFFFFFF;
        if (et >= 2 || elen == 0) continue;
        total += (elen + bs - 1) / bs;
    }
    uint32_t *list = kmalloc((total ? total : 1) * sizeof(uint32_t));
    if (!list) return -1;
    uint32_t n = 0;
    for (uint32_t p = 0; p + 8 <= l_ad; p += 8) {
        uint32_t raw = rd32(fe + ad_area + p);
        uint32_t et = raw >> 30, elen = raw & 0x3FFFFFFF;
        uint32_t blk = rd32(fe + ad_area + p + 4);
        if (et >= 2 || elen == 0) continue;
        uint32_t nb = (elen + bs - 1) / bs;
        for (uint32_t k = 0; k < nb; k++) list[n++] = blk + k;
    }
    *out_list = list;
    return (int)n;
}

static int64_t udf_file_write(vnode_t *n, const void *buf, size_t len, uint64_t offset) {
    if (!n || !buf) return -EINVAL;
    if (len == 0) return 0;
    udf_node_t *nd = n->fs_data;
    if (!nd || nd->is_dir) return -EISDIR;
    udf_fs_t *fs = nd->fs;
    uint32_t bs = fs->block_size;

    uint8_t *fe = kmalloc(bs);
    if (!fe) return -ENOMEM;
    if (udf_read_sector(fs->dev, nd->fe_lba, fe) < 0) { kfree(fe); return -EIO; }
    int efe = (rd16(fe) == TAG_EFE);
    uint32_t l_ea = efe ? rd32(fe + 208) : rd32(fe + 168);
    uint32_t ad_area = (efe ? 216u : 176u) + l_ea;
    uint32_t inline_cap = bs - ad_area;
    int ad_type = fe[34] & 7;
    uint64_t cur_size = rd64(fe + 56);
    uint64_t new_size = (offset + len > cur_size) ? (offset + len) : cur_size;
    if (new_size > 8u * 1024 * 1024) { kfree(fe); return -EFBIG; }

    if (new_size <= inline_cap) {
        uint8_t *data = kmalloc((size_t)new_size);
        if (!data) { kfree(fe); return -ENOMEM; }
        memset(data, 0, (size_t)new_size);
        uint64_t keep = (cur_size < new_size) ? cur_size : new_size;
        if (keep) udf_read_range(fs, nd->fe_lba, data, keep, 0);
        memcpy(data + offset, buf, len);
        udf_free_fe_extents(fs, fe);
        int r = udf_write_fe_data(fs, fe, nd->fe_lba, data, new_size, 0);
        kfree(data); kfree(fe);
        if (r < 0) return r;
        nd->size = new_size; n->size = new_size;
        return (int64_t)len;
    }

    if (ad_type == 1) { kfree(fe); return udf_resize_write(n, buf, len, offset, 0); }

    uint32_t content_blocks = (uint32_t)((cur_size + bs - 1) / bs);
    uint32_t disk_blocks = (ad_type == 3) ? 0 : content_blocks;
    uint32_t needed = (uint32_t)((new_size + bs - 1) / bs);
    uint32_t max_ads = inline_cap / 8;

    uint32_t *nb_list = kmalloc(needed * sizeof(uint32_t));
    if (!nb_list) { kfree(fe); return -ENOMEM; }

    uint8_t *inln = NULL;
    if (ad_type == 3) {
        if (cur_size) {
            inln = kmalloc((size_t)cur_size);
            if (!inln) { kfree(nb_list); kfree(fe); return -ENOMEM; }
            memcpy(inln, fe + ad_area, (size_t)cur_size);
        }
    } else {
        uint32_t *cur = NULL;
        int nc = udf_load_blocks(fs, fe, &cur);
        if (nc < 0) { kfree(nb_list); kfree(fe); return -ENOMEM; }
        for (uint32_t i = 0; i < disk_blocks && i < (uint32_t)nc; i++) nb_list[i] = cur[i];
        if (cur) kfree(cur);
    }

    int allocated = 0;
    for (uint32_t i = disk_blocks; i < needed; i++) {
        int b = udf_alloc_block(fs);
        if (b < 0) {
            for (uint32_t k = disk_blocks; k < i; k++) udf_free_block(fs, nb_list[k]);
            if (inln) kfree(inln);
            kfree(nb_list); kfree(fe);
            return -ENOSPC;
        }
        nb_list[i] = (uint32_t)b;
        allocated = 1;
    }

    uint8_t *sec = kmalloc(bs);
    if (inln) {
        uint32_t done = 0;
        while (done < cur_size) {
            uint32_t bi = done / bs;
            memset(sec, 0, bs);
            uint32_t cpy = (uint32_t)((cur_size - done > bs) ? bs : (cur_size - done));
            memcpy(sec, inln + done, cpy);
            udf_write_sector(fs->dev, fs->part_start + nb_list[bi], sec);
            done += cpy;
        }
        kfree(inln);
    }

    for (uint32_t bi = content_blocks; bi < needed; bi++) {
        uint64_t bstart = (uint64_t)bi * bs;
        if (bstart + bs <= offset || bstart >= offset + len) {
            memset(sec, 0, bs);
            udf_write_sector(fs->dev, fs->part_start + nb_list[bi], sec);
        }
    }

    const uint8_t *src = buf;
    uint64_t pos = offset, end = offset + len;
    while (pos < end) {
        uint32_t bi = (uint32_t)(pos / bs);
        uint32_t boff = (uint32_t)(pos % bs);
        uint32_t cpy = bs - boff;
        if (cpy > end - pos) cpy = (uint32_t)(end - pos);
        if (boff != 0 || cpy != bs) {
            if (bi < content_blocks) udf_read_sector(fs->dev, fs->part_start + nb_list[bi], sec);
            else memset(sec, 0, bs);
            memcpy(sec + boff, src, cpy);
        } else {
            memcpy(sec, src, bs);
        }
        udf_write_sector(fs->dev, fs->part_start + nb_list[bi], sec);
        src += cpy; pos += cpy;
    }
    kfree(sec);

    uint8_t *ad = fe + ad_area;
    memset(fe + ad_area, 0, bs - ad_area);
    uint32_t l_ad = 0, i = 0;
    int overflow = 0;
    while (i < needed) {
        if (l_ad / 8 >= max_ads) { overflow = 1; break; }
        uint32_t start = nb_list[i], run = 1;
        while (i + run < needed && nb_list[i + run] == start + run) run++;
        uint64_t eb = (i + run >= needed) ? (new_size - (uint64_t)i * bs) : (uint64_t)run * bs;
        wr32(ad + l_ad, (uint32_t)eb);
        wr32(ad + l_ad + 4, start);
        l_ad += 8;
        i += run;
    }
    kfree(nb_list);
    if (overflow) { kfree(fe); return -EFBIG; }

    fe[34] = (uint8_t)(fe[34] & ~7);
    wr64(fe + 56, new_size);
    if (efe) { wr64(fe + 64, new_size); wr64(fe + 72, needed); wr32(fe + 212, l_ad); }
    else     { wr64(fe + 64, needed); wr32(fe + 172, l_ad); }
    udf_finalize_tag(fs, fe, efe ? TAG_EFE : TAG_FE,
                     (uint16_t)(ad_area + l_ad - 16), nd->fe_lba - fs->part_start);
    int r = udf_write_sector(fs->dev, nd->fe_lba, fe);
    kfree(fe);
    if (r < 0) return -EIO;
    nd->size = new_size; n->size = new_size;
    if (allocated) udf_flush_lvid(fs);
    return (int64_t)len;
}

static int udf_truncate(vnode_t *n, uint64_t new_size) {
    if (!n) return -EINVAL;
    return (int)udf_resize_write(n, NULL, 0, new_size, 1);
}

static int udf_dir_add_fid(udf_fs_t *fs, uint32_t dir_fe_lba, const char *name,
                           uint32_t child_block, int child_is_dir) {
    uint32_t bs = fs->block_size;
    uint8_t *fe = kmalloc(bs);
    if (!fe) return -ENOMEM;
    if (udf_read_sector(fs->dev, dir_fe_lba, fe) < 0) { kfree(fe); return -EIO; }
    uint64_t dir_size = rd64(fe + 56);
    uint32_t dir_block = dir_fe_lba - fs->part_start;

    size_t nlen = strlen(name);
    uint8_t  l_fi = (uint8_t)(nlen + 1);
    uint32_t fid_len = (uint32_t)((38 + l_fi + 3) & ~3);
    uint64_t new_size = dir_size + fid_len;
    if (new_size > 4u * 1024 * 1024) { kfree(fe); return -ENOSPC; }

    uint8_t *buf = kmalloc((size_t)new_size);
    if (!buf) { kfree(fe); return -ENOMEM; }
    if (dir_size && udf_read_range(fs, dir_fe_lba, buf, dir_size, 0) < 0) {
        kfree(buf); kfree(fe); return -EIO;
    }

    uint8_t *fid = buf + dir_size;
    memset(fid, 0, fid_len);
    wr16(fid + 16, 1);
    fid[18] = (uint8_t)(child_is_dir ? 0x02 : 0x00);
    fid[19] = l_fi;
    wr32(fid + 20, bs);
    wr32(fid + 24, child_block);
    wr16(fid + 28, 0);
    wr16(fid + 36, 0);
    fid[38] = 8;
    memcpy(fid + 39, name, nlen);
    udf_finalize_tag(fs, fid, TAG_FID, (uint16_t)(fid_len - 16), dir_block);

    udf_free_fe_extents(fs, fe);
    int r = udf_write_fe_data(fs, fe, dir_fe_lba, buf, new_size, 1);
    kfree(buf);
    kfree(fe);
    return r;
}

static int udf_create(vnode_t *dir, const char *name, uint32_t mode, vnode_t **out) {
    if (!dir || !name || !out) return -EINVAL;
    udf_node_t *dnd = dir->fs_data;
    if (!dnd || !dnd->is_dir) return -ENOTDIR;
    udf_fs_t *fs = dnd->fs;
    uint32_t bs = fs->block_size;
    (void)mode;

    size_t nlen = strlen(name);
    if (nlen == 0 || nlen > 200) return -EINVAL;

    int fe_block = udf_alloc_block(fs);
    if (fe_block < 0) return -ENOSPC;
    uint32_t fe_lba = fs->part_start + (uint32_t)fe_block;

    uint8_t *fe = kmalloc(bs);
    if (!fe) return -ENOMEM;
    if (udf_read_sector(fs->dev, dnd->fe_lba, fe) < 0) { kfree(fe); return -EIO; }
    int efe = (rd16(fe) == TAG_EFE);
    fe[27] = 5;
    fe[34] = (uint8_t)((fe[34] & ~7) | 3);
    fe[35] = 0;
    wr32(fe + 44, 0x7884);
    wr16(fe + 48, 1);
    wr64(fe + 56, 0);
    uint32_t l_ea = efe ? rd32(fe + 208) : rd32(fe + 168);
    uint32_t ad_area = (efe ? 216u : 176u) + l_ea;
    if (efe) { wr64(fe + 64, 0); wr64(fe + 72, 0); wr32(fe + 212, 0); }
    else     { wr64(fe + 64, 0); wr32(fe + 172, 0); }
    memset(fe + ad_area, 0, bs - ad_area);
    uint64_t uid = (uint64_t)fe_block + 16;
    wr64(fe + (efe ? 200 : 160), uid);
    if (fs->next_uniqid <= uid) fs->next_uniqid = uid + 1;
    udf_finalize_tag(fs, fe, efe ? TAG_EFE : TAG_FE,
                     (uint16_t)(ad_area - 16), (uint32_t)fe_block);
    int wr = udf_write_sector(fs->dev, fe_lba, fe);
    kfree(fe);
    if (wr < 0) return -EIO;

    int r = udf_dir_add_fid(fs, dnd->fe_lba, name, (uint32_t)fe_block, 0);
    if (r < 0) return r;

    vnode_t *vn = udf_alloc_vnode(fs, fe_lba, 0, false);
    if (!vn) return -ENOMEM;
    *out = vn;
    udf_flush_lvid(fs);
    return 0;
}

static int udf_mkdir(vnode_t *dir, const char *name, uint32_t mode) {
    if (!dir || !name) return -EINVAL;
    udf_node_t *dnd = dir->fs_data;
    if (!dnd || !dnd->is_dir) return -ENOTDIR;
    udf_fs_t *fs = dnd->fs;
    uint32_t bs = fs->block_size;
    (void)mode;
    size_t nlen = strlen(name);
    if (nlen == 0 || nlen > 200) return -EINVAL;

    int dblk = udf_alloc_block(fs);
    if (dblk < 0) return -ENOSPC;
    uint32_t dlba = fs->part_start + (uint32_t)dblk;

    uint8_t *fe = kmalloc(bs);
    if (!fe) return -ENOMEM;
    if (udf_read_sector(fs->dev, dnd->fe_lba, fe) < 0) { kfree(fe); return -EIO; }
    int efe = (rd16(fe) == TAG_EFE);
    fe[27] = 4;
    fe[34] = (uint8_t)((fe[34] & ~7) | 3);
    fe[35] = 0;
    wr16(fe + 48, 1);
    uint32_t l_ea = efe ? rd32(fe + 208) : rd32(fe + 168);
    uint32_t ad_area = (efe ? 216u : 176u) + l_ea;
    memset(fe + ad_area, 0, bs - ad_area);

    uint8_t *pfid = fe + ad_area;
    uint32_t pfid_len = (38 + 3) & ~3u;
    memset(pfid, 0, pfid_len);
    wr16(pfid + 16, 1);
    pfid[18] = 0x0A;
    pfid[19] = 0;
    wr32(pfid + 20, bs);
    wr32(pfid + 24, dnd->fe_lba - fs->part_start);
    wr16(pfid + 28, 0);
    wr16(pfid + 36, 0);
    udf_finalize_tag(fs, pfid, TAG_FID, (uint16_t)(pfid_len - 16), (uint32_t)dblk);

    uint64_t dir_data = pfid_len;
    wr64(fe + 56, dir_data);
    if (efe) { wr64(fe + 64, dir_data); wr64(fe + 72, 0); wr32(fe + 212, (uint32_t)dir_data); }
    else     { wr64(fe + 64, 0);        wr32(fe + 172, (uint32_t)dir_data); }
    uint64_t uid = (uint64_t)dblk + 16;
    wr64(fe + (efe ? 200 : 160), uid);
    if (fs->next_uniqid <= uid) fs->next_uniqid = uid + 1;
    udf_finalize_tag(fs, fe, efe ? TAG_EFE : TAG_FE,
                     (uint16_t)(ad_area + dir_data - 16), (uint32_t)dblk);
    int wr = udf_write_sector(fs->dev, dlba, fe);
    kfree(fe);
    if (wr < 0) return -EIO;

    int r = udf_dir_add_fid(fs, dnd->fe_lba, name, (uint32_t)dblk, 1);
    if (r == 0) udf_flush_lvid(fs);
    return r;
}

static int udf_dir_remove_fid(udf_fs_t *fs, uint32_t dir_fe_lba, const char *name,
                              uint32_t *out_child, int *out_is_dir) {
    uint32_t bs = fs->block_size;
    uint8_t *fe = kmalloc(bs);
    if (!fe) return -ENOMEM;
    if (udf_read_sector(fs->dev, dir_fe_lba, fe) < 0) { kfree(fe); return -EIO; }
    uint64_t dir_size = rd64(fe + 56);
    if (dir_size == 0 || dir_size > 4u * 1024 * 1024) { kfree(fe); return -ENOENT; }

    uint8_t *buf = kmalloc((size_t)dir_size);
    if (!buf) { kfree(fe); return -ENOMEM; }
    if (udf_read_range(fs, dir_fe_lba, buf, dir_size, 0) < 0) { kfree(buf); kfree(fe); return -EIO; }

    uint32_t off = 0;
    int found = 0;
    while (off + 38 <= dir_size) {
        uint8_t *f = buf + off;
        if (rd16(f) != TAG_FID) break;
        uint8_t chars = f[18];
        uint8_t l_fi = f[19];
        uint16_t l_iu = rd16(f + 36);
        uint32_t fid_len = (uint32_t)((38 + l_iu + l_fi + 3) & ~3);
        if (off + fid_len > dir_size) break;
        if (!(chars & 0x0C) && l_fi > 0) {
            char nm[256];
            udf_fid_name(buf, off + 38 + l_iu, l_fi, nm, sizeof(nm));
            if (strcmp(nm, name) == 0) {
                if (out_child) *out_child = rd32(f + 24);
                if (out_is_dir) *out_is_dir = (chars & 0x02) != 0;
                memmove(f, f + fid_len, (size_t)(dir_size - off - fid_len));
                dir_size -= fid_len;
                found = 1;
                break;
            }
        }
        off += fid_len;
    }
    if (!found) { kfree(buf); kfree(fe); return -ENOENT; }

    udf_free_fe_extents(fs, fe);
    int r = udf_write_fe_data(fs, fe, dir_fe_lba, buf, dir_size, 1);
    kfree(buf);
    kfree(fe);
    return r < 0 ? r : 0;
}

static void udf_free_child(udf_fs_t *fs, uint32_t child_block) {
    if (!child_block) return;
    uint8_t *cfe = kmalloc(fs->block_size);
    if (cfe && udf_read_sector(fs->dev, fs->part_start + child_block, cfe) == 0)
        udf_free_fe_extents(fs, cfe);
    if (cfe) kfree(cfe);
    udf_free_block(fs, child_block);
}

static int udf_unlink(vnode_t *dir, const char *name) {
    if (!dir || !name) return -EINVAL;
    udf_node_t *dnd = dir->fs_data;
    if (!dnd || !dnd->is_dir) return -ENOTDIR;
    udf_fs_t *fs = dnd->fs;
    uint32_t child = 0;
    int r = udf_dir_remove_fid(fs, dnd->fe_lba, name, &child, NULL);
    if (r < 0) return r;
    udf_free_child(fs, child);
    udf_flush_lvid(fs);
    return 0;
}

static int udf_rename(vnode_t *src_dir, const char *src_name,
                      vnode_t *dst_dir, const char *dst_name) {
    if (!src_dir || !src_name || !dst_dir || !dst_name) return -EINVAL;
    udf_node_t *snd = src_dir->fs_data;
    udf_node_t *dnd = dst_dir->fs_data;
    if (!snd || !dnd || !snd->is_dir || !dnd->is_dir) return -ENOTDIR;
    udf_fs_t *fs = snd->fs;
    if (dnd->fs != fs) return -EXDEV;

    uint32_t old_child = 0;
    if (udf_dir_remove_fid(fs, dnd->fe_lba, dst_name, &old_child, NULL) == 0)
        udf_free_child(fs, old_child);

    uint32_t child = 0;
    int is_dir = 0;
    int r = udf_dir_remove_fid(fs, snd->fe_lba, src_name, &child, &is_dir);
    if (r < 0) return r;
    r = udf_dir_add_fid(fs, dnd->fe_lba, dst_name, child, is_dir);
    if (r == 0) udf_flush_lvid(fs);
    return r;
}

static const vnode_ops_t udf_file_ops = {
    .read     = udf_file_read,
    .write    = udf_file_write,
    .truncate = udf_truncate,
    .stat     = udf_stat,
    .ref      = udf_ref,
    .unref    = udf_unref,
};
static const vnode_ops_t udf_dir_ops = {
    .lookup  = udf_lookup,
    .readdir = udf_readdir,
    .create  = udf_create,
    .mkdir   = udf_mkdir,
    .unlink  = udf_unlink,
    .rename  = udf_rename,
    .stat    = udf_stat,
    .ref     = udf_ref,
    .unref   = udf_unref,
};

int udf_detect(blkdev_t *dev) {
    if (!dev) return 0;
    for (uint32_t k = 0; k < 16; k++) {
        uint8_t vrs[8] = {0};
        if (blkdev_read(dev, 32768 + (uint64_t)k * 2048, vrs, 6) != 0) break;
        if (memcmp(vrs + 1, "NSR02", 5) == 0 || memcmp(vrs + 1, "NSR03", 5) == 0) return 1;
        if (memcmp(vrs + 1, "TEA01", 5) == 0) break;
        if (vrs[0] == 0 &&
            memcmp(vrs + 1, "BEA01", 5) != 0 &&
            memcmp(vrs + 1, "CD001", 5) != 0 &&
            memcmp(vrs + 1, "BOOT2", 5) != 0) break;
    }
    return 0;
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
    uint32_t bitmap_block = 0, bitmap_len = 0;
    uint32_t integ_loc = 0;

    uint32_t nsec = mvds_len / UDF_SECTOR;
    if (nsec > 64) nsec = 64;
    for (uint32_t i = 0; i < nsec; i++) {
        if (udf_read_sector(dev, mvds_loc + i, sec) < 0) break;
        uint16_t tag = rd16(sec);
        if (tag == 0) continue;
        if (tag == TAG_PD) {
            part_start = rd32(sec + 188);
            part_len   = rd32(sec + 192);
            bitmap_len   = rd32(sec + 64) & 0x3FFFFFFF;
            bitmap_block = rd32(sec + 68);
        } else if (tag == TAG_LVD) {
            block_size = rd32(sec + 212);
            fsd_len    = rd32(sec + 248);
            fsd_block  = rd32(sec + 248 + 4);
            integ_loc  = rd32(sec + 436);
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
    if (block_size != dev->sector_size) {
        serial_printf("[udf] %s: block size %u != device sector %u (unsupported)\n",
                      dev->name, block_size, dev->sector_size);
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
    fs->root_block = root_block;

    if (bitmap_len) {
        fs->bitmap_lba = part_start + bitmap_block;
        if (udf_read_sector(dev, fs->bitmap_lba, sec) == 0 && rd16(sec) == TAG_SBD) {
            fs->bitmap_nbits  = rd32(sec + 16);
            fs->bitmap_nbytes = rd32(sec + 20);
        } else {
            fs->bitmap_lba = 0;
        }
    }

    fs->next_uniqid = 16;
    if (integ_loc && udf_read_sector(dev, integ_loc, sec) == 0 && rd16(sec) == TAG_LVID) {
        fs->lvid_lba    = integ_loc;
        fs->lvid_nparts = rd32(sec + 72);
        fs->free_blocks = rd32(sec + 80);
        uint64_t uid    = rd64(sec + 40);
        if (uid > fs->next_uniqid) fs->next_uniqid = uid;
    }

    if (udf_read_sector(dev, root_fe, sec) < 0) { free(fs); kfree(sec); return NULL; }
    fs->desc_ver   = rd16(sec + 2);
    fs->tag_serial = rd16(sec + 6);
    bool is_dir; uint64_t size;
    udf_fe_info(sec, &is_dir, &size);

    serial_printf("[udf] %s: mounted (part_start=%u root_fe=%u bitmap_lba=%u nbits=%u ver=%u ser=%u)\n",
                  dev->name, part_start, root_fe, fs->bitmap_lba,
                  fs->bitmap_nbits, fs->desc_ver, fs->tag_serial);

    kfree(sec);
    vnode_t *root = udf_alloc_vnode(fs, root_fe, size, true);
    if (!root) { free(fs); return NULL; }
    return root;
}
