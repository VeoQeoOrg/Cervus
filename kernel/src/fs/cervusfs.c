#include "../../include/fs/cervusfs.h"
#include "../../include/fs/vfs.h"
#include "../../include/drivers/blkdev.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include "../../include/syscall/errno.h"
#include <string.h>

//mini ext2 FS btw

static int sb_read(cervusfs_t *fs) {
    return blkdev_read(fs->dev, 0, &fs->sb, sizeof(fs->sb));
}
static int sb_write(cervusfs_t *fs) {
    return blkdev_write(fs->dev, 0, &fs->sb, sizeof(fs->sb));
}

static int bitmap_read(cervusfs_t *fs) {
    size_t ib_size = (size_t)fs->sb.inode_bitmap_sectors * CERVUSFS_BLOCK_SIZE;
    size_t db_size = (size_t)fs->sb.data_bitmap_sectors  * CERVUSFS_BLOCK_SIZE;
    fs->inode_bitmap = kmalloc(ib_size);
    fs->data_bitmap  = kmalloc(db_size);
    if (!fs->inode_bitmap || !fs->data_bitmap) return -ENOMEM;
    int r = blkdev_read(fs->dev,
        (uint64_t)fs->sb.inode_bitmap_start * CERVUSFS_BLOCK_SIZE,
        fs->inode_bitmap, ib_size);
    if (r < 0) return r;
    r = blkdev_read(fs->dev,
        (uint64_t)fs->sb.data_bitmap_start * CERVUSFS_BLOCK_SIZE,
        fs->data_bitmap, db_size);
    return r;
}

static int bitmap_flush(cervusfs_t *fs) {
    size_t ib_size = (size_t)fs->sb.inode_bitmap_sectors * CERVUSFS_BLOCK_SIZE;
    size_t db_size = (size_t)fs->sb.data_bitmap_sectors  * CERVUSFS_BLOCK_SIZE;
    int r = blkdev_write(fs->dev,
        (uint64_t)fs->sb.inode_bitmap_start * CERVUSFS_BLOCK_SIZE,
        fs->inode_bitmap, ib_size);
    if (r < 0) return r;
    r = blkdev_write(fs->dev,
        (uint64_t)fs->sb.data_bitmap_start * CERVUSFS_BLOCK_SIZE,
        fs->data_bitmap, db_size);
    return r;
}

static bool bmp_test(const uint8_t *bmp, uint32_t bit) {
    return (bmp[bit / 8] >> (bit % 8)) & 1;
}
static void bmp_set(uint8_t *bmp, uint32_t bit) {
    bmp[bit / 8] |= (1 << (bit % 8));
}
static void bmp_clear(uint8_t *bmp, uint32_t bit) {
    bmp[bit / 8] &= ~(1 << (bit % 8));
}

static int32_t alloc_inode(cervusfs_t *fs) {
    for (uint32_t i = 0; i < fs->sb.inode_count; i++) {
        if (!bmp_test(fs->inode_bitmap, i)) {
            bmp_set(fs->inode_bitmap, i);
            fs->sb.free_inodes--;
            fs->dirty = true;
            return (int32_t)i;
        }
    }
    return -ENOSPC;
}

static void free_inode(cervusfs_t *fs, uint32_t ino) {
    bmp_clear(fs->inode_bitmap, ino);
    fs->sb.free_inodes++;
    fs->dirty = true;
}

static int32_t alloc_block(cervusfs_t *fs) {
    for (uint32_t i = 0; i < fs->sb.data_block_count; i++) {
        if (!bmp_test(fs->data_bitmap, i)) {
            bmp_set(fs->data_bitmap, i);
            fs->sb.free_blocks--;
            fs->dirty = true;
            return (int32_t)i;
        }
    }
    return -ENOSPC;
}

static void free_block(cervusfs_t *fs, uint32_t blk) {
    bmp_clear(fs->data_bitmap, blk);
    fs->sb.free_blocks++;
    fs->dirty = true;
}

static int inode_read(cervusfs_t *fs, uint32_t ino, cervusfs_inode_t *out) {
    uint64_t off = (uint64_t)fs->sb.inode_table_start * CERVUSFS_BLOCK_SIZE
                 + (uint64_t)ino * CERVUSFS_INODE_SIZE;
    return blkdev_read(fs->dev, off, out, sizeof(*out));
}

static int inode_write(cervusfs_t *fs, uint32_t ino, const cervusfs_inode_t *in) {
    uint64_t off = (uint64_t)fs->sb.inode_table_start * CERVUSFS_BLOCK_SIZE
                 + (uint64_t)ino * CERVUSFS_INODE_SIZE;
    return blkdev_write(fs->dev, off, in, sizeof(*in));
}

static uint64_t data_block_offset(cervusfs_t *fs, uint32_t blk_idx) {
    return ((uint64_t)fs->sb.data_start + blk_idx) * CERVUSFS_BLOCK_SIZE;
}

typedef struct {
    cervusfs_t *fs;
    uint32_t    ino;
} cfs_vnode_data_t;

static const vnode_ops_t cfs_file_ops;
static const vnode_ops_t cfs_dir_ops;

static vnode_t *cfs_make_vnode(cervusfs_t *fs, uint32_t ino, cervusfs_inode_t *di) {
    vnode_t *v = kzalloc(sizeof(vnode_t));
    if (!v) return NULL;
    cfs_vnode_data_t *pd = kzalloc(sizeof(cfs_vnode_data_t));
    if (!pd) { kfree(v); return NULL; }
    pd->fs  = fs;
    pd->ino = ino;
    v->type     = (di->type == CERVUSFS_INODE_DIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
    v->mode     = di->mode;
    v->ino      = ino;
    v->size     = di->size;
    v->fs_data  = pd;
    v->ops      = (di->type == CERVUSFS_INODE_DIR) ? &cfs_dir_ops : &cfs_file_ops;
    v->refcount = 1;
    return v;
}

static int64_t cfs_file_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    cfs_vnode_data_t *pd = node->fs_data;
    cervusfs_t *fs = pd->fs;
    cervusfs_inode_t di;
    int r = inode_read(fs, pd->ino, &di);
    if (r < 0) return r;
    if (offset >= di.size) return 0;
    if (offset + len > di.size) len = di.size - (size_t)offset;
    if (len == 0) return 0;
    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;
    uint8_t sector_buf[CERVUSFS_BLOCK_SIZE];
    while (done < len) {
        uint32_t cur_off    = (uint32_t)(offset + done);
        uint32_t blk_index  = cur_off / CERVUSFS_BLOCK_SIZE;
        uint32_t blk_off    = cur_off % CERVUSFS_BLOCK_SIZE;
        if (blk_index >= CERVUSFS_DIRECT_BLOCKS) break;
        if (di.blocks[blk_index] == 0) break;
        r = blkdev_read(fs->dev, data_block_offset(fs, di.blocks[blk_index]),
                        sector_buf, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        size_t chunk = CERVUSFS_BLOCK_SIZE - blk_off;
        if (chunk > len - done) chunk = len - done;
        memcpy(dst + done, sector_buf + blk_off, chunk);
        done += chunk;
    }
    return (int64_t)done;
}

static int64_t cfs_file_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    cfs_vnode_data_t *pd = node->fs_data;
    cervusfs_t *fs = pd->fs;
    cervusfs_inode_t di;
    int r = inode_read(fs, pd->ino, &di);
    if (r < 0) return r;
    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;
    uint8_t sector_buf[CERVUSFS_BLOCK_SIZE];
    while (done < len) {
        uint32_t cur_off   = (uint32_t)(offset + done);
        uint32_t blk_index = cur_off / CERVUSFS_BLOCK_SIZE;
        uint32_t blk_off   = cur_off % CERVUSFS_BLOCK_SIZE;
        if (blk_index >= CERVUSFS_DIRECT_BLOCKS) break;
        if (di.blocks[blk_index] == 0) {
            int32_t newblk = alloc_block(fs);
            if (newblk < 0) return newblk;
            di.blocks[blk_index] = (uint32_t)newblk;
            memset(sector_buf, 0, CERVUSFS_BLOCK_SIZE);
        } else {
            r = blkdev_read(fs->dev, data_block_offset(fs, di.blocks[blk_index]),
                            sector_buf, CERVUSFS_BLOCK_SIZE);
            if (r < 0) return r;
        }
        size_t chunk = CERVUSFS_BLOCK_SIZE - blk_off;
        if (chunk > len - done) chunk = len - done;
        memcpy(sector_buf + blk_off, src + done, chunk);
        r = blkdev_write(fs->dev, data_block_offset(fs, di.blocks[blk_index]),
                         sector_buf, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        done += chunk;
    }
    uint32_t new_end = (uint32_t)(offset + done);
    if (new_end > di.size) {
        di.size = new_end;
        node->size = new_end;
    }
    inode_write(fs, pd->ino, &di);
    fs->dirty = true;
    return (int64_t)done;
}

static int cfs_file_truncate(vnode_t *node, uint64_t new_size) {
    cfs_vnode_data_t *pd = node->fs_data;
    cervusfs_t *fs = pd->fs;
    cervusfs_inode_t di;
    int r = inode_read(fs, pd->ino, &di);
    if (r < 0) return r;
    if (new_size == 0) {
        for (int i = 0; i < CERVUSFS_DIRECT_BLOCKS; i++) {
            if (di.blocks[i]) {
                free_block(fs, di.blocks[i]);
                di.blocks[i] = 0;
            }
        }
    }
    di.size = (uint32_t)new_size;
    node->size = new_size;
    inode_write(fs, pd->ino, &di);
    fs->dirty = true;
    return 0;
}

static int cfs_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = node->type;
    out->st_mode = node->mode;
    out->st_size = node->size;
    out->st_blocks = (node->size + 511) / 512;
    return 0;
}

static void cfs_ref(vnode_t *node) { (void)node; }

static void cfs_unref(vnode_t *node) {
    if (node->fs_data) kfree(node->fs_data);
    kfree(node);
}

static const vnode_ops_t cfs_file_ops = {
    .read     = cfs_file_read,
    .write    = cfs_file_write,
    .truncate = cfs_file_truncate,
    .stat     = cfs_stat,
    .ref      = cfs_ref,
    .unref    = cfs_unref,
};

static int cfs_dir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    cfs_vnode_data_t *pd = dir->fs_data;
    cervusfs_t *fs = pd->fs;
    cervusfs_inode_t di;
    int r = inode_read(fs, pd->ino, &di);
    if (r < 0) return r;
    cervusfs_dirent_disk_t entries[CERVUSFS_DIRENTS_PER_BLOCK];
    for (int b = 0; b < CERVUSFS_DIRECT_BLOCKS; b++) {
        if (di.blocks[b] == 0) continue;
        r = blkdev_read(fs->dev, data_block_offset(fs, di.blocks[b]),
                        entries, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        for (int e = 0; e < CERVUSFS_DIRENTS_PER_BLOCK; e++) {
            if (entries[e].inode == 0) continue;
            if (strncmp(entries[e].name, name, CERVUSFS_NAME_MAX) == 0) {
                cervusfs_inode_t child_di;
                r = inode_read(fs, entries[e].inode, &child_di);
                if (r < 0) return r;
                *out = cfs_make_vnode(fs, entries[e].inode, &child_di);
                if (!*out) return -ENOMEM;
                return 0;
            }
        }
    }
    return -ENOENT;
}

static int cfs_dir_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    cfs_vnode_data_t *pd = dir->fs_data;
    cervusfs_t *fs = pd->fs;
    cervusfs_inode_t di;
    int r = inode_read(fs, pd->ino, &di);
    if (r < 0) return r;
    cervusfs_dirent_disk_t entries[CERVUSFS_DIRENTS_PER_BLOCK];
    uint64_t cur = 0;
    for (int b = 0; b < CERVUSFS_DIRECT_BLOCKS; b++) {
        if (di.blocks[b] == 0) continue;
        r = blkdev_read(fs->dev, data_block_offset(fs, di.blocks[b]),
                        entries, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        for (int e = 0; e < CERVUSFS_DIRENTS_PER_BLOCK; e++) {
            if (entries[e].inode == 0) continue;
            if (cur == index) {
                out->d_ino  = entries[e].inode;
                out->d_type = entries[e].type;
                strncpy(out->d_name, entries[e].name, VFS_MAX_NAME - 1);
                out->d_name[VFS_MAX_NAME - 1] = '\0';
                return 0;
            }
            cur++;
        }
    }
    return -ENOENT;
}

static int cfs_dir_add_entry(cervusfs_t *fs, uint32_t dir_ino, uint32_t child_ino, uint8_t type, const char *name) {
    cervusfs_inode_t di;
    int r = inode_read(fs, dir_ino, &di);
    if (r < 0) return r;
    cervusfs_dirent_disk_t entries[CERVUSFS_DIRENTS_PER_BLOCK];
    for (int b = 0; b < CERVUSFS_DIRECT_BLOCKS; b++) {
        if (di.blocks[b] == 0) continue;
        r = blkdev_read(fs->dev, data_block_offset(fs, di.blocks[b]),
                        entries, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        for (int e = 0; e < CERVUSFS_DIRENTS_PER_BLOCK; e++) {
            if (entries[e].inode == 0) {
                entries[e].inode    = child_ino;
                entries[e].type     = type;
                entries[e].name_len = (uint8_t)strlen(name);
                strncpy(entries[e].name, name, CERVUSFS_NAME_MAX - 1);
                entries[e].name[CERVUSFS_NAME_MAX - 1] = '\0';
                r = blkdev_write(fs->dev, data_block_offset(fs, di.blocks[b]),
                             entries, CERVUSFS_BLOCK_SIZE);
                if (r < 0) return r;
                di.size++;
                return inode_write(fs, dir_ino, &di);
            }
        }
    }
    for (int b = 0; b < CERVUSFS_DIRECT_BLOCKS; b++) {
        if (di.blocks[b] != 0) continue;
        int32_t newblk = alloc_block(fs);
        if (newblk < 0) return (int)newblk;
        di.blocks[b] = (uint32_t)newblk;
        memset(entries, 0, sizeof(entries));
        entries[0].inode    = child_ino;
        entries[0].type     = type;
        entries[0].name_len = (uint8_t)strlen(name);
        strncpy(entries[0].name, name, CERVUSFS_NAME_MAX - 1);
        entries[0].name[CERVUSFS_NAME_MAX - 1] = '\0';
        r = blkdev_write(fs->dev, data_block_offset(fs, (uint32_t)newblk),
                     entries, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        di.size++;
        return inode_write(fs, dir_ino, &di);
    }
    return -ENOSPC;
}

static int cfs_dir_mkdir(vnode_t *dir, const char *name, uint32_t mode) {
    cfs_vnode_data_t *pd = dir->fs_data;
    cervusfs_t *fs = pd->fs;
    vnode_t *existing = NULL;
    if (cfs_dir_lookup(dir, name, &existing) == 0) {
        vnode_unref(existing);
        return -EEXIST;
    }
    int32_t ino = alloc_inode(fs);
    if (ino < 0) return (int)ino;
    cervusfs_inode_t new_di;
    memset(&new_di, 0, sizeof(new_di));
    new_di.type = CERVUSFS_INODE_DIR;
    new_di.mode = mode ? (uint16_t)mode : 0755;
    int r = inode_write(fs, (uint32_t)ino, &new_di);
    if (r < 0) { free_inode(fs, (uint32_t)ino); return r; }
    r = cfs_dir_add_entry(fs, pd->ino, (uint32_t)ino, CERVUSFS_INODE_DIR, name);
    if (r < 0) { free_inode(fs, (uint32_t)ino); return r; }
    dir->size++;
    fs->dirty = true;
    return 0;
}

static int cfs_dir_create(vnode_t *dir, const char *name, uint32_t mode, vnode_t **out) {
    cfs_vnode_data_t *pd = dir->fs_data;
    cervusfs_t *fs = pd->fs;
    if (cfs_dir_lookup(dir, name, out) == 0)
        return 0;
    int32_t ino = alloc_inode(fs);
    if (ino < 0) return (int)ino;
    cervusfs_inode_t new_di;
    memset(&new_di, 0, sizeof(new_di));
    new_di.type = CERVUSFS_INODE_FILE;
    new_di.mode = mode ? (uint16_t)mode : 0644;
    inode_write(fs, (uint32_t)ino, &new_di);
    int r = cfs_dir_add_entry(fs, pd->ino, (uint32_t)ino, CERVUSFS_INODE_FILE, name);
    if (r < 0) { free_inode(fs, (uint32_t)ino); return r; }
    dir->size++;
    *out = cfs_make_vnode(fs, (uint32_t)ino, &new_di);
    if (!*out) return -ENOMEM;
    fs->dirty = true;
    return 0;
}

static int cfs_dir_unlink(vnode_t *dir, const char *name) {
    cfs_vnode_data_t *pd = dir->fs_data;
    cervusfs_t *fs = pd->fs;
    cervusfs_inode_t di;
    int r = inode_read(fs, pd->ino, &di);
    if (r < 0) return r;
    cervusfs_dirent_disk_t entries[CERVUSFS_DIRENTS_PER_BLOCK];
    for (int b = 0; b < CERVUSFS_DIRECT_BLOCKS; b++) {
        if (di.blocks[b] == 0) continue;
        r = blkdev_read(fs->dev, data_block_offset(fs, di.blocks[b]),
                        entries, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        for (int e = 0; e < CERVUSFS_DIRENTS_PER_BLOCK; e++) {
            if (entries[e].inode == 0) continue;
            if (strncmp(entries[e].name, name, CERVUSFS_NAME_MAX) == 0) {
                uint32_t child_ino = entries[e].inode;
                cervusfs_inode_t child_di;
                inode_read(fs, child_ino, &child_di);
                for (int i = 0; i < CERVUSFS_DIRECT_BLOCKS; i++) {
                    if (child_di.blocks[i])
                        free_block(fs, child_di.blocks[i]);
                }
                free_inode(fs, child_ino);
                memset(&entries[e], 0, sizeof(entries[e]));
                blkdev_write(fs->dev, data_block_offset(fs, di.blocks[b]),
                             entries, CERVUSFS_BLOCK_SIZE);
                di.size--;
                inode_write(fs, pd->ino, &di);
                dir->size--;
                fs->dirty = true;
                return 0;
            }
        }
    }
    return -ENOENT;
}

static int cfs_dir_rename(vnode_t *src_dir, const char *src_name,
                          vnode_t *dst_dir, const char *dst_name) {
    cfs_vnode_data_t *spd = src_dir->fs_data;
    cfs_vnode_data_t *dpd = dst_dir->fs_data;
    cervusfs_t *fs = spd->fs;

    cervusfs_inode_t src_di;
    int r = inode_read(fs, spd->ino, &src_di);
    if (r < 0) return r;

    cervusfs_dirent_disk_t entries[CERVUSFS_DIRENTS_PER_BLOCK];
    uint32_t child_ino = 0;
    uint8_t  child_type = 0;
    bool found = false;

    for (int b = 0; b < CERVUSFS_DIRECT_BLOCKS && !found; b++) {
        if (src_di.blocks[b] == 0) continue;
        r = blkdev_read(fs->dev, data_block_offset(fs, src_di.blocks[b]),
                        entries, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        for (int e = 0; e < CERVUSFS_DIRENTS_PER_BLOCK; e++) {
            if (entries[e].inode == 0) continue;
            if (strncmp(entries[e].name, src_name, CERVUSFS_NAME_MAX) == 0) {
                child_ino  = entries[e].inode;
                child_type = entries[e].type;
                found = true;
                break;
            }
        }
    }
    if (!found) return -ENOENT;

    vnode_t *existing = NULL;
    if (cfs_dir_lookup(dst_dir, dst_name, &existing) == 0) {
        vnode_unref(existing);
        if (dst_dir->ops && dst_dir->ops->unlink)
            dst_dir->ops->unlink(dst_dir, dst_name);
    }

    r = cfs_dir_add_entry(fs, dpd->ino, child_ino, child_type, dst_name);
    if (r < 0) return r;
    dst_dir->size++;

    if (src_dir->ops && src_dir->ops->unlink) {}
    r = inode_read(fs, spd->ino, &src_di);
    if (r < 0) return r;
    for (int b = 0; b < CERVUSFS_DIRECT_BLOCKS; b++) {
        if (src_di.blocks[b] == 0) continue;
        r = blkdev_read(fs->dev, data_block_offset(fs, src_di.blocks[b]),
                        entries, CERVUSFS_BLOCK_SIZE);
        if (r < 0) return r;
        bool dirty = false;
        for (int e = 0; e < CERVUSFS_DIRENTS_PER_BLOCK; e++) {
            if (entries[e].inode == child_ino &&
                strncmp(entries[e].name, src_name, CERVUSFS_NAME_MAX) == 0) {
                memset(&entries[e], 0, sizeof(entries[e]));
                dirty = true;
                break;
            }
        }
        if (dirty) {
            blkdev_write(fs->dev, data_block_offset(fs, src_di.blocks[b]),
                         entries, CERVUSFS_BLOCK_SIZE);
            src_di.size--;
            inode_write(fs, spd->ino, &src_di);
            src_dir->size--;
            break;
        }
    }

    fs->dirty = true;
    return 0;
}


static const vnode_ops_t cfs_dir_ops = {
    .lookup  = cfs_dir_lookup,
    .readdir = cfs_dir_readdir,
    .mkdir   = cfs_dir_mkdir,
    .create  = cfs_dir_create,
    .unlink  = cfs_dir_unlink,
    .rename  = cfs_dir_rename,
    .stat    = cfs_stat,
    .ref     = cfs_ref,
    .unref   = cfs_unref,
};

int cervusfs_format(blkdev_t *dev, const char *label) {
    if (!dev) return -EINVAL;
    uint32_t total = (uint32_t)(dev->sector_count);
    if (total < 64) return -ENOSPC;
    uint32_t inode_count      = (total < 2048) ? 64 : 256;
    uint32_t ib_sectors       = (inode_count + CERVUSFS_BLOCK_SIZE * 8 - 1) / (CERVUSFS_BLOCK_SIZE * 8);
    uint32_t it_sectors       = (inode_count + CERVUSFS_INODES_PER_SECTOR - 1) / CERVUSFS_INODES_PER_SECTOR;
    uint32_t overhead = 1 + ib_sectors;
    uint32_t data_blocks_est  = total - overhead - it_sectors - 2;
    uint32_t db_sectors       = (data_blocks_est + CERVUSFS_BLOCK_SIZE * 8 - 1) / (CERVUSFS_BLOCK_SIZE * 8);
    uint32_t ib_start = 1;
    uint32_t db_start = ib_start + ib_sectors;
    uint32_t it_start = db_start + db_sectors;
    uint32_t data_start = it_start + it_sectors;
    uint32_t data_block_count = total - data_start;
    cervusfs_super_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = CERVUSFS_MAGIC;
    sb.version = CERVUSFS_VERSION;
    sb.total_sectors = total;
    sb.inode_count = inode_count;
    sb.data_block_count = data_block_count;
    sb.inode_bitmap_start = ib_start;
    sb.inode_bitmap_sectors = ib_sectors;
    sb.data_bitmap_start = db_start;
    sb.data_bitmap_sectors = db_sectors;
    sb.inode_table_start = it_start;
    sb.inode_table_sectors = it_sectors;
    sb.data_start = data_start;
    sb.free_inodes = inode_count - 1;
    sb.free_blocks = data_block_count - 1;
    sb.root_inode = 0;
    if (label) {
        strncpy((char *)sb.label, label, 15);
        sb.label[15] = '\0';
    }
    int r = blkdev_write(dev, 0, &sb, sizeof(sb));
    if (r < 0) return r;
    uint8_t zero_sec[CERVUSFS_BLOCK_SIZE];
    memset(zero_sec, 0, sizeof(zero_sec));
    for (uint32_t s = ib_start; s < ib_start + ib_sectors; s++)
        blkdev_write(dev, (uint64_t)s * CERVUSFS_BLOCK_SIZE, zero_sec, CERVUSFS_BLOCK_SIZE);
    for (uint32_t s = db_start; s < db_start + db_sectors; s++)
        blkdev_write(dev, (uint64_t)s * CERVUSFS_BLOCK_SIZE, zero_sec, CERVUSFS_BLOCK_SIZE);
    uint8_t ib_first[CERVUSFS_BLOCK_SIZE];
    memset(ib_first, 0, sizeof(ib_first));
    ib_first[0] = 0x01;
    blkdev_write(dev, (uint64_t)ib_start * CERVUSFS_BLOCK_SIZE, ib_first, CERVUSFS_BLOCK_SIZE);

    uint8_t db_first[CERVUSFS_BLOCK_SIZE];
    memset(db_first, 0, sizeof(db_first));
    db_first[0] = 0x01;
    blkdev_write(dev, (uint64_t)db_start * CERVUSFS_BLOCK_SIZE, db_first, CERVUSFS_BLOCK_SIZE);
    for (uint32_t s = it_start; s < it_start + it_sectors; s++)
        blkdev_write(dev, (uint64_t)s * CERVUSFS_BLOCK_SIZE, zero_sec, CERVUSFS_BLOCK_SIZE);
    cervusfs_inode_t root_ino;
    memset(&root_ino, 0, sizeof(root_ino));
    root_ino.type = CERVUSFS_INODE_DIR;
    root_ino.mode = 0755;
    blkdev_write(dev, (uint64_t)it_start * CERVUSFS_BLOCK_SIZE, &root_ino, sizeof(root_ino));
    serial_printf("[CervusFS] formatted: %u sectors, %u inodes, %u data blocks\n",
                  total, inode_count, data_block_count);
    return 0;
}

vnode_t *cervusfs_mount(blkdev_t *dev) {
    if (!dev) return NULL;
    cervusfs_t *fs = kzalloc(sizeof(cervusfs_t));
    if (!fs) return NULL;
    fs->dev = dev;
    fs->dirty = false;
    if (sb_read(fs) < 0) { kfree(fs); return NULL; }
    if (fs->sb.magic != CERVUSFS_MAGIC) {
        serial_printf("[CervusFS] bad magic: 0x%x (expected 0x%x)\n",
                      fs->sb.magic, CERVUSFS_MAGIC);
        kfree(fs);
        return NULL;
    }
    if (bitmap_read(fs) < 0) {
        if (fs->inode_bitmap) kfree(fs->inode_bitmap);
        if (fs->data_bitmap)  kfree(fs->data_bitmap);
        kfree(fs);
        return NULL;
    }
    cervusfs_inode_t root_di;
    if (inode_read(fs, 0, &root_di) < 0) {
        kfree(fs->inode_bitmap);
        kfree(fs->data_bitmap);
        kfree(fs);
        return NULL;
    }
    vnode_t *root = cfs_make_vnode(fs, 0, &root_di);
    if (!root) {
        kfree(fs->inode_bitmap);
        kfree(fs->data_bitmap);
        kfree(fs);
        return NULL;
    }
    serial_printf("[CervusFS] mounted '%s': %u inodes (%u free), %u blocks (%u free)\n",
                  fs->sb.label,
                  fs->sb.inode_count, fs->sb.free_inodes,
                  fs->sb.data_block_count, fs->sb.free_blocks);
    return root;
}

void cervusfs_sync(cervusfs_t *fs) {
    if (!fs || !fs->dirty) return;
    bitmap_flush(fs);
    sb_write(fs);
    fs->dirty = false;
}

void cervusfs_unmount(cervusfs_t *fs) {
    if (!fs) return;
    cervusfs_sync(fs);
    if (fs->inode_bitmap) kfree(fs->inode_bitmap);
    if (fs->data_bitmap)  kfree(fs->data_bitmap);
    kfree(fs);
}