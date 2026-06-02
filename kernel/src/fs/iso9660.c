#include "../../include/fs/iso9660.h"
#include "../../include/fs/vfs.h"
#include "../../include/drivers/disk/blkdev.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct {
    blkdev_t *dev;
    uint32_t  block_size;
    uint32_t  total_blocks;
    uint32_t  root_extent_lba;
    uint32_t  root_size;
    char      volume_id[33];
} iso9660_fs_t;

typedef struct {
    iso9660_fs_t *fs;
    uint32_t      extent_lba;
    uint32_t      size_bytes;
    bool          is_dir;
} iso9660_node_t;

static const vnode_ops_t iso9660_file_ops;
static const vnode_ops_t iso9660_dir_ops;

static uint64_t g_iso9660_ino = 5000;

static void iso9660_common_ref(vnode_t *n) {
    if (!n) return;
    __atomic_add_fetch(&n->refcount, 1, __ATOMIC_SEQ_CST);
}

static void iso9660_common_unref(vnode_t *n) {
    if (!n) return;
    int nrc = __atomic_sub_fetch(&n->refcount, 1, __ATOMIC_SEQ_CST);
    if (nrc <= 0) {
        if (n->fs_data) free(n->fs_data);
        free(n);
    }
}

static vnode_t *iso9660_alloc_vnode(iso9660_fs_t *fs, uint32_t extent_lba,
                                    uint32_t size_bytes, bool is_dir)
{
    vnode_t *vn = calloc(1, sizeof(vnode_t));
    if (!vn) return NULL;
    iso9660_node_t *nd = calloc(1, sizeof(iso9660_node_t));
    if (!nd) { free(vn); return NULL; }

    nd->fs         = fs;
    nd->extent_lba = extent_lba;
    nd->size_bytes = size_bytes;
    nd->is_dir     = is_dir;

    vn->ino      = g_iso9660_ino++;
    vn->refcount = 1;
    vn->fs_data  = nd;
    vn->size     = size_bytes;

    if (is_dir) {
        vn->type = VFS_NODE_DIR;
        vn->mode = 0555;
        vn->ops  = &iso9660_dir_ops;
    } else {
        vn->type = VFS_NODE_FILE;
        vn->mode = 0444;
        vn->ops  = &iso9660_file_ops;
    }
    return vn;
}

static int iso9660_read_block(iso9660_fs_t *fs, uint32_t lba, void *buf) {
    return fs->dev->ops->read_sectors(fs->dev, lba, 1, buf);
}

static void iso9660_normalize_name(const char *raw, size_t raw_len, char *out, size_t out_cap) {
    size_t n = raw_len;
    for (size_t i = 0; i < raw_len; i++) {
        if (raw[i] == ';') { n = i; break; }
    }
    while (n > 0 && raw[n - 1] == '.') n--;
    if (n >= out_cap) n = out_cap - 1;
    for (size_t i = 0; i < n; i++) {
        char c = raw[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[n] = '\0';
}

static bool iso9660_name_matches(const char *raw, size_t raw_len, const char *target) {
    char buf[256];
    iso9660_normalize_name(raw, raw_len, buf, sizeof(buf));

    char want[256];
    size_t tl = strlen(target);
    if (tl >= sizeof(want)) tl = sizeof(want) - 1;
    for (size_t i = 0; i < tl; i++) {
        char c = target[i];
        want[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    want[tl] = '\0';

    return strcmp(buf, want) == 0;
}

static int iso9660_scan_dir(iso9660_fs_t *fs, uint32_t dir_lba, uint32_t dir_size,
                            const char *target_name, uint64_t want_index,
                            uint32_t *out_lba, uint32_t *out_size, bool *out_is_dir,
                            char *out_name_buf, size_t out_name_cap,
                            uint64_t *visited_count)
{
    uint8_t *block = kmalloc(fs->block_size);
    if (!block) return -ENOMEM;

    uint32_t blocks = (dir_size + fs->block_size - 1) / fs->block_size;
    uint64_t seen = 0;
    int found = -ENOENT;

    for (uint32_t b = 0; b < blocks; b++) {
        if (iso9660_read_block(fs, dir_lba + b, block) < 0) {
            kfree(block);
            return -EIO;
        }

        uint32_t off = 0;
        while (off < fs->block_size) {
            uint8_t rec_len = block[off];
            if (rec_len == 0) break;
            if (off + rec_len > fs->block_size) break;

            uint32_t ext_lba;
            uint32_t ext_size;
            memcpy(&ext_lba,  block + off + 2,  4);
            memcpy(&ext_size, block + off + 10, 4);
            uint8_t flags    = block[off + 25];
            uint8_t name_len = block[off + 32];
            const char *name = (const char *)(block + off + 33);

            bool skip = false;
            if (name_len == 1 && (name[0] == 0 || name[0] == 1)) skip = true;

            if (!skip) {
                if (target_name) {
                    if (iso9660_name_matches(name, name_len, target_name)) {
                        *out_lba    = ext_lba;
                        *out_size   = ext_size;
                        *out_is_dir = (flags & 0x02) != 0;
                        kfree(block);
                        return 0;
                    }
                } else {
                    if (seen == want_index) {
                        if (out_name_buf && out_name_cap)
                            iso9660_normalize_name(name, name_len, out_name_buf, out_name_cap);
                        *out_lba    = ext_lba;
                        *out_size   = ext_size;
                        *out_is_dir = (flags & 0x02) != 0;
                        kfree(block);
                        if (visited_count) *visited_count = seen;
                        return 0;
                    }
                    seen++;
                }
            }

            off += rec_len;
        }
    }

    kfree(block);
    if (visited_count) *visited_count = seen;
    return found;
}

static int iso9660_dir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    if (!dir || !name || !out) return -EINVAL;
    iso9660_node_t *nd = (iso9660_node_t *)dir->fs_data;
    if (!nd || !nd->is_dir) return -ENOTDIR;

    uint32_t lba = 0, size = 0;
    bool is_dir = false;
    int r = iso9660_scan_dir(nd->fs, nd->extent_lba, nd->size_bytes,
                             name, 0, &lba, &size, &is_dir, NULL, 0, NULL);
    if (r < 0) return r;

    vnode_t *child = iso9660_alloc_vnode(nd->fs, lba, size, is_dir);
    if (!child) return -ENOMEM;
    *out = child;
    return 0;
}

static int iso9660_dir_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    if (!dir || !out) return -EINVAL;
    iso9660_node_t *nd = (iso9660_node_t *)dir->fs_data;
    if (!nd || !nd->is_dir) return -ENOTDIR;

    uint32_t lba = 0, size = 0;
    bool is_dir = false;
    char nm[VFS_MAX_NAME];
    int r = iso9660_scan_dir(nd->fs, nd->extent_lba, nd->size_bytes,
                             NULL, index, &lba, &size, &is_dir,
                             nm, sizeof(nm), NULL);
    if (r < 0) return r;

    memset(out, 0, sizeof(*out));
    strncpy(out->d_name, nm, VFS_MAX_NAME - 1);
    out->d_type = is_dir ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->d_ino  = (uint64_t)lba;
    return 0;
}

static int iso9660_common_stat(vnode_t *n, vfs_stat_t *out) {
    if (!n || !out) return -EINVAL;
    iso9660_node_t *nd = (iso9660_node_t *)n->fs_data;
    memset(out, 0, sizeof(*out));
    out->st_ino    = n->ino;
    out->st_type   = n->type;
    out->st_mode   = n->mode;
    out->st_size   = nd ? nd->size_bytes : 0;
    out->st_blocks = nd ? (nd->size_bytes + 511) / 512 : 0;
    return 0;
}

static int64_t iso9660_file_read(vnode_t *n, void *buf, size_t len, uint64_t offset) {
    if (!n || !buf) return -EINVAL;
    iso9660_node_t *nd = (iso9660_node_t *)n->fs_data;
    if (!nd || nd->is_dir) return -EISDIR;

    if (offset >= nd->size_bytes) return 0;
    uint64_t remaining = (uint64_t)nd->size_bytes - offset;
    if (len > remaining) len = (size_t)remaining;
    if (len == 0) return 0;

    iso9660_fs_t *fs = nd->fs;
    uint32_t bs = fs->block_size;
    uint8_t *block = kmalloc(bs);
    if (!block) return -ENOMEM;

    uint8_t *dst = (uint8_t *)buf;
    size_t copied = 0;

    while (copied < len) {
        uint64_t cur = offset + copied;
        uint32_t bidx = (uint32_t)(cur / bs);
        uint32_t boff = (uint32_t)(cur % bs);
        uint32_t can  = bs - boff;
        if (can > len - copied) can = (uint32_t)(len - copied);

        int r = iso9660_read_block(fs, nd->extent_lba + bidx, block);
        if (r < 0) { kfree(block); return r; }

        memcpy(dst + copied, block + boff, can);
        copied += can;
    }

    kfree(block);
    return (int64_t)copied;
}

static const vnode_ops_t iso9660_file_ops = {
    .read  = iso9660_file_read,
    .stat  = iso9660_common_stat,
    .ref   = iso9660_common_ref,
    .unref = iso9660_common_unref,
};

static const vnode_ops_t iso9660_dir_ops = {
    .lookup  = iso9660_dir_lookup,
    .readdir = iso9660_dir_readdir,
    .stat    = iso9660_common_stat,
    .ref     = iso9660_common_ref,
    .unref   = iso9660_common_unref,
};

vnode_t *iso9660_mount(blkdev_t *dev) {
    if (!dev) return NULL;

    uint8_t *pvd = kmalloc(ISO9660_SECTOR_SIZE);
    if (!pvd) return NULL;

    int r = dev->ops->read_sectors(dev, ISO9660_PVD_LBA, 1, pvd);
    if (r < 0) {
        serial_printf("[iso9660] %s: cannot read PVD at LBA %u: %d\n",
                      dev->name, ISO9660_PVD_LBA, r);
        kfree(pvd);
        return NULL;
    }

    if (pvd[0] != 0x01 ||
        pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' ||
        pvd[4] != '0' || pvd[5] != '1')
    {
        serial_printf("[iso9660] %s: not an ISO9660 volume (no CD001 signature)\n",
                      dev->name);
        kfree(pvd);
        return NULL;
    }

    iso9660_fs_t *fs = calloc(1, sizeof(iso9660_fs_t));
    if (!fs) { kfree(pvd); return NULL; }

    fs->dev = dev;

    uint16_t bs;
    memcpy(&bs, pvd + 128, 2);
    fs->block_size = bs ? bs : ISO9660_SECTOR_SIZE;

    memcpy(&fs->total_blocks, pvd + 80, 4);

    uint8_t *root_rec = pvd + ISO9660_ROOT_REC_OFFSET;
    memcpy(&fs->root_extent_lba, root_rec + 2,  4);
    memcpy(&fs->root_size,       root_rec + 10, 4);

    memcpy(fs->volume_id, pvd + 40, 32);
    fs->volume_id[32] = '\0';
    for (int i = 31; i >= 0 && fs->volume_id[i] == ' '; i--) fs->volume_id[i] = '\0';

    serial_printf("[iso9660] %s: '%s' bs=%u total=%u root_lba=%u root_size=%u\n",
                  dev->name, fs->volume_id, fs->block_size, fs->total_blocks,
                  fs->root_extent_lba, fs->root_size);

    kfree(pvd);

    vnode_t *root = iso9660_alloc_vnode(fs, fs->root_extent_lba, fs->root_size, true);
    if (!root) { free(fs); return NULL; }
    return root;
}
