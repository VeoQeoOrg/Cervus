#include "../../include/fs/ext2.h"
#include "../../include/fs/vfs.h"
#include "../../include/drivers/blkdev.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include "../../include/syscall/errno.h"
#include <string.h>

static int block_read(ext2_t *fs, uint32_t block, void *buf) {
    return blkdev_read(fs->dev, (uint64_t)block * fs->block_size, buf, fs->block_size);
}

static int block_write(ext2_t *fs, uint32_t block, const void *buf) {
    return blkdev_write(fs->dev, (uint64_t)block * fs->block_size, buf, fs->block_size);
}

static int sb_flush(ext2_t *fs) {
    return blkdev_write(fs->dev, EXT2_SUPER_OFFSET, &fs->sb, sizeof(fs->sb));
}

static int gdt_flush(ext2_t *fs) {
    uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    return blkdev_write(fs->dev, (uint64_t)gdt_block * fs->block_size,
                        fs->gdt, fs->groups_count * sizeof(ext2_group_desc_t));
}

static int inode_read(ext2_t *fs, uint32_t ino, ext2_inode_t *out) {
    if (ino == 0) return -EINVAL;
    uint32_t idx = ino - 1;
    uint32_t group = idx / fs->sb.s_inodes_per_group;
    uint32_t local = idx % fs->sb.s_inodes_per_group;
    if (group >= fs->groups_count) return -EINVAL;
    uint64_t off = (uint64_t)fs->gdt[group].bg_inode_table * fs->block_size
                 + (uint64_t)local * fs->inode_size;
    return blkdev_read(fs->dev, off, out, sizeof(*out));
}

static int inode_write(ext2_t *fs, uint32_t ino, const ext2_inode_t *in) {
    if (ino == 0) return -EINVAL;
    uint32_t idx = ino - 1;
    uint32_t group = idx / fs->sb.s_inodes_per_group;
    uint32_t local = idx % fs->sb.s_inodes_per_group;
    if (group >= fs->groups_count) return -EINVAL;
    uint64_t off = (uint64_t)fs->gdt[group].bg_inode_table * fs->block_size
                 + (uint64_t)local * fs->inode_size;
    return blkdev_write(fs->dev, off, in, sizeof(*in));
}

static bool bmp_test(const uint8_t *bmp, uint32_t bit) {
    return (bmp[bit / 8] >> (bit % 8)) & 1;
}
static void bmp_set(uint8_t *bmp, uint32_t bit) {
    bmp[bit / 8] |= (uint8_t)(1 << (bit % 8));
}
static void bmp_clear(uint8_t *bmp, uint32_t bit) {
    bmp[bit / 8] &= (uint8_t)~(1 << (bit % 8));
}

static int32_t alloc_inode(ext2_t *fs) {
    uint8_t *bmp = kmalloc(fs->block_size);
    if (!bmp) return -ENOMEM;
    for (uint32_t g = 0; g < fs->groups_count; g++) {
        if (fs->gdt[g].bg_free_inodes_count == 0) continue;
        block_read(fs, fs->gdt[g].bg_inode_bitmap, bmp);
        for (uint32_t i = 0; i < fs->sb.s_inodes_per_group; i++) {
            if (!bmp_test(bmp, i)) {
                bmp_set(bmp, i);
                block_write(fs, fs->gdt[g].bg_inode_bitmap, bmp);
                fs->gdt[g].bg_free_inodes_count--;
                fs->sb.s_free_inodes_count--;
                fs->dirty = true;
                kfree(bmp);
                return (int32_t)(g * fs->sb.s_inodes_per_group + i + 1);
            }
        }
    }
    kfree(bmp);
    return -ENOSPC;
}

static void free_inode(ext2_t *fs, uint32_t ino) {
    uint32_t idx = ino - 1;
    uint32_t group = idx / fs->sb.s_inodes_per_group;
    uint32_t local = idx % fs->sb.s_inodes_per_group;
    uint8_t *bmp = kmalloc(fs->block_size);
    if (!bmp) return;
    block_read(fs, fs->gdt[group].bg_inode_bitmap, bmp);
    bmp_clear(bmp, local);
    block_write(fs, fs->gdt[group].bg_inode_bitmap, bmp);
    fs->gdt[group].bg_free_inodes_count++;
    fs->sb.s_free_inodes_count++;
    fs->dirty = true;
    kfree(bmp);
}

static int32_t alloc_block(ext2_t *fs) {
    uint8_t *bmp = kmalloc(fs->block_size);
    if (!bmp) return -ENOMEM;
    for (uint32_t g = 0; g < fs->groups_count; g++) {
        if (fs->gdt[g].bg_free_blocks_count == 0) continue;
        block_read(fs, fs->gdt[g].bg_block_bitmap, bmp);
        for (uint32_t i = 0; i < fs->sb.s_blocks_per_group; i++) {
            uint32_t abs_block = g * fs->sb.s_blocks_per_group + i + fs->sb.s_first_data_block;
            if (abs_block >= fs->sb.s_blocks_count) break;
            if (!bmp_test(bmp, i)) {
                bmp_set(bmp, i);
                block_write(fs, fs->gdt[g].bg_block_bitmap, bmp);
                fs->gdt[g].bg_free_blocks_count--;
                fs->sb.s_free_blocks_count--;
                fs->dirty = true;
                kfree(bmp);
                return (int32_t)abs_block;
            }
        }
    }
    kfree(bmp);
    return -ENOSPC;
}

static void free_block(ext2_t *fs, uint32_t blk) {
    if (blk < fs->sb.s_first_data_block) return;
    uint32_t adj = blk - fs->sb.s_first_data_block;
    uint32_t group = adj / fs->sb.s_blocks_per_group;
    uint32_t local = adj % fs->sb.s_blocks_per_group;
    if (group >= fs->groups_count) return;
    uint8_t *bmp = kmalloc(fs->block_size);
    if (!bmp) return;
    block_read(fs, fs->gdt[group].bg_block_bitmap, bmp);
    bmp_clear(bmp, local);
    block_write(fs, fs->gdt[group].bg_block_bitmap, bmp);
    fs->gdt[group].bg_free_blocks_count++;
    fs->sb.s_free_blocks_count++;
    fs->dirty = true;
    kfree(bmp);
}

static int32_t get_block_num(ext2_t *fs, ext2_inode_t *di, uint32_t file_block) {
    if (file_block < EXT2_NDIR_BLOCKS)
        return (int32_t)di->i_block[file_block];
    uint32_t ppb = fs->ptrs_per_block;
    file_block -= EXT2_NDIR_BLOCKS;
    if (file_block < ppb) {
        if (di->i_block[EXT2_IND_BLOCK] == 0) return 0;
        uint32_t *ind = kmalloc(fs->block_size);
        if (!ind) return -ENOMEM;
        block_read(fs, di->i_block[EXT2_IND_BLOCK], ind);
        int32_t ret = (int32_t)ind[file_block];
        kfree(ind);
        return ret;
    }
    file_block -= ppb;
    if (file_block < ppb * ppb) {
        if (di->i_block[EXT2_DIND_BLOCK] == 0) return 0;
        uint32_t *dind = kmalloc(fs->block_size);
        if (!dind) return -ENOMEM;
        block_read(fs, di->i_block[EXT2_DIND_BLOCK], dind);
        uint32_t i1 = file_block / ppb;
        uint32_t i2 = file_block % ppb;
        if (dind[i1] == 0) { kfree(dind); return 0; }
        uint32_t ib = dind[i1];
        kfree(dind);
        uint32_t *ind = kmalloc(fs->block_size);
        if (!ind) return -ENOMEM;
        block_read(fs, ib, ind);
        int32_t ret = (int32_t)ind[i2];
        kfree(ind);
        return ret;
    }
    return -EFBIG;
}

static int set_block_num(ext2_t *fs, ext2_inode_t *di, uint32_t file_block, uint32_t disk_block) {
    if (file_block < EXT2_NDIR_BLOCKS) {
        di->i_block[file_block] = disk_block;
        return 0;
    }
    uint32_t ppb = fs->ptrs_per_block;
    file_block -= EXT2_NDIR_BLOCKS;
    if (file_block < ppb) {
        if (di->i_block[EXT2_IND_BLOCK] == 0) {
            int32_t nb = alloc_block(fs);
            if (nb < 0) return nb;
            di->i_block[EXT2_IND_BLOCK] = (uint32_t)nb;
            uint8_t *z = kzalloc(fs->block_size);
            block_write(fs, (uint32_t)nb, z);
            kfree(z);
        }
        uint32_t *ind = kmalloc(fs->block_size);
        if (!ind) return -ENOMEM;
        block_read(fs, di->i_block[EXT2_IND_BLOCK], ind);
        ind[file_block] = disk_block;
        block_write(fs, di->i_block[EXT2_IND_BLOCK], ind);
        kfree(ind);
        return 0;
    }
    file_block -= ppb;
    if (file_block < ppb * ppb) {
        if (di->i_block[EXT2_DIND_BLOCK] == 0) {
            int32_t nb = alloc_block(fs);
            if (nb < 0) return nb;
            di->i_block[EXT2_DIND_BLOCK] = (uint32_t)nb;
            uint8_t *z = kzalloc(fs->block_size);
            block_write(fs, (uint32_t)nb, z);
            kfree(z);
        }
        uint32_t *dind = kmalloc(fs->block_size);
        if (!dind) return -ENOMEM;
        block_read(fs, di->i_block[EXT2_DIND_BLOCK], dind);
        uint32_t i1 = file_block / ppb;
        uint32_t i2 = file_block % ppb;
        if (dind[i1] == 0) {
            int32_t nb = alloc_block(fs);
            if (nb < 0) { kfree(dind); return nb; }
            dind[i1] = (uint32_t)nb;
            block_write(fs, di->i_block[EXT2_DIND_BLOCK], dind);
            uint8_t *z = kzalloc(fs->block_size);
            block_write(fs, (uint32_t)nb, z);
            kfree(z);
        }
        uint32_t ib = dind[i1];
        kfree(dind);
        uint32_t *ind = kmalloc(fs->block_size);
        if (!ind) return -ENOMEM;
        block_read(fs, ib, ind);
        ind[i2] = disk_block;
        block_write(fs, ib, ind);
        kfree(ind);
        return 0;
    }
    return -EFBIG;
}

static const vnode_ops_t ext2_file_ops;
static const vnode_ops_t ext2_dir_ops;

static vnode_t *ext2_make_vnode(ext2_t *fs, uint32_t ino, ext2_inode_t *di) {
    vnode_t *v = kzalloc(sizeof(vnode_t));
    if (!v) return NULL;
    ext2_vdata_t *vd = kzalloc(sizeof(ext2_vdata_t));
    if (!vd) { kfree(v); return NULL; }
    vd->fs  = fs;
    vd->ino = ino;
    if ((di->i_mode & 0xF000) == EXT2_S_IFDIR) {
        v->type = VFS_NODE_DIR;
        v->ops  = &ext2_dir_ops;
    } else {
        v->type = VFS_NODE_FILE;
        v->ops  = &ext2_file_ops;
    }
    v->mode     = di->i_mode & 0x0FFF;
    v->uid      = di->i_uid;
    v->gid      = di->i_gid;
    v->ino      = ino;
    v->size     = di->i_size;
    v->fs_data  = vd;
    v->refcount = 1;
    return v;
}

static int64_t ext2_file_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    ext2_vdata_t *vd = node->fs_data;
    ext2_t *fs = vd->fs;
    ext2_inode_t di;
    int r = inode_read(fs, vd->ino, &di);
    if (r < 0) return r;
    if (offset >= di.i_size) return 0;
    if (offset + len > di.i_size) len = di.i_size - (size_t)offset;
    if (len == 0) return 0;
    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;
    uint8_t *bb = kmalloc(fs->block_size);
    if (!bb) return -ENOMEM;
    while (done < len) {
        uint32_t co = (uint32_t)(offset + done);
        uint32_t fb = co / fs->block_size;
        uint32_t bo = co % fs->block_size;
        int32_t db = get_block_num(fs, &di, fb);
        if (db <= 0) {
            if (db < 0) { kfree(bb); return db; }
            memset(bb, 0, fs->block_size);
        } else {
            r = block_read(fs, (uint32_t)db, bb);
            if (r < 0) { kfree(bb); return r; }
        }
        size_t ch = fs->block_size - bo;
        if (ch > len - done) ch = len - done;
        memcpy(dst + done, bb + bo, ch);
        done += ch;
    }
    kfree(bb);
    return (int64_t)done;
}

static int64_t ext2_file_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    ext2_vdata_t *vd = node->fs_data;
    ext2_t *fs = vd->fs;
    ext2_inode_t di;
    int r = inode_read(fs, vd->ino, &di);
    if (r < 0) return r;
    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;
    uint8_t *bb = kmalloc(fs->block_size);
    if (!bb) return -ENOMEM;
    while (done < len) {
        uint32_t co = (uint32_t)(offset + done);
        uint32_t fb = co / fs->block_size;
        uint32_t bo = co % fs->block_size;
        int32_t db = get_block_num(fs, &di, fb);
        if (db == 0) {
            int32_t nb = alloc_block(fs);
            if (nb < 0) { kfree(bb); return (done > 0) ? (int64_t)done : nb; }
            set_block_num(fs, &di, fb, (uint32_t)nb);
            db = nb;
            memset(bb, 0, fs->block_size);
            di.i_blocks += fs->block_size / 512;
        } else if (db < 0) {
            kfree(bb); return db;
        } else {
            if (bo != 0 || (len - done) < fs->block_size)
                block_read(fs, (uint32_t)db, bb);
        }
        size_t ch = fs->block_size - bo;
        if (ch > len - done) ch = len - done;
        memcpy(bb + bo, src + done, ch);
        block_write(fs, (uint32_t)db, bb);
        done += ch;
    }
    uint32_t ne = (uint32_t)(offset + done);
    if (ne > di.i_size) { di.i_size = ne; node->size = ne; }
    inode_write(fs, vd->ino, &di);
    fs->dirty = true;
    kfree(bb);
    return (int64_t)done;
}

static void free_all_blocks(ext2_t *fs, ext2_inode_t *di) {
    for (int i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (di->i_block[i]) { free_block(fs, di->i_block[i]); di->i_block[i] = 0; }
    }
    if (di->i_block[EXT2_IND_BLOCK]) {
        uint32_t *ind = kmalloc(fs->block_size);
        if (ind) {
            block_read(fs, di->i_block[EXT2_IND_BLOCK], ind);
            for (uint32_t i = 0; i < fs->ptrs_per_block; i++)
                if (ind[i]) free_block(fs, ind[i]);
            kfree(ind);
        }
        free_block(fs, di->i_block[EXT2_IND_BLOCK]);
        di->i_block[EXT2_IND_BLOCK] = 0;
    }
    if (di->i_block[EXT2_DIND_BLOCK]) {
        uint32_t *dind = kmalloc(fs->block_size);
        if (dind) {
            block_read(fs, di->i_block[EXT2_DIND_BLOCK], dind);
            for (uint32_t i = 0; i < fs->ptrs_per_block; i++) {
                if (dind[i]) {
                    uint32_t *ind = kmalloc(fs->block_size);
                    if (ind) {
                        block_read(fs, dind[i], ind);
                        for (uint32_t j = 0; j < fs->ptrs_per_block; j++)
                            if (ind[j]) free_block(fs, ind[j]);
                        kfree(ind);
                    }
                    free_block(fs, dind[i]);
                }
            }
            kfree(dind);
        }
        free_block(fs, di->i_block[EXT2_DIND_BLOCK]);
        di->i_block[EXT2_DIND_BLOCK] = 0;
    }
    di->i_blocks = 0;
}

static int ext2_file_truncate(vnode_t *node, uint64_t new_size) {
    ext2_vdata_t *vd = node->fs_data;
    ext2_t *fs = vd->fs;
    ext2_inode_t di;
    int r = inode_read(fs, vd->ino, &di);
    if (r < 0) return r;
    if (new_size == 0) free_all_blocks(fs, &di);
    di.i_size = (uint32_t)new_size;
    node->size = new_size;
    inode_write(fs, vd->ino, &di);
    fs->dirty = true;
    return 0;
}

static int ext2_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = node->type;
    out->st_mode = node->mode;
    out->st_uid  = node->uid;
    out->st_gid  = node->gid;
    out->st_size = node->size;
    out->st_blocks = (node->size + 511) / 512;
    return 0;
}

static void ext2_vnode_ref(vnode_t *node)  { (void)node; }
static void ext2_vnode_unref(vnode_t *node) {
    if (node->fs_data) kfree(node->fs_data);
    kfree(node);
}

static const vnode_ops_t ext2_file_ops = {
    .read     = ext2_file_read,
    .write    = ext2_file_write,
    .truncate = ext2_file_truncate,
    .stat     = ext2_stat,
    .ref      = ext2_vnode_ref,
    .unref    = ext2_vnode_unref,
};

static int ext2_dir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    ext2_vdata_t *vd = dir->fs_data;
    ext2_t *fs = vd->fs;
    ext2_inode_t di;
    int r = inode_read(fs, vd->ino, &di);
    if (r < 0) return r;
    uint8_t *bb = kmalloc(fs->block_size);
    if (!bb) return -ENOMEM;
    size_t nl = strlen(name);
    uint32_t ds = di.i_size;
    uint32_t pos = 0, fb = 0;
    while (pos < ds) {
        int32_t db = get_block_num(fs, &di, fb);
        if (db <= 0) { fb++; pos += fs->block_size; continue; }
        block_read(fs, (uint32_t)db, bb);
        uint32_t off = 0;
        while (off < fs->block_size && (pos + off) < ds) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(bb + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == nl && memcmp(de->name, name, nl) == 0) {
                ext2_inode_t ci;
                inode_read(fs, de->inode, &ci);
                *out = ext2_make_vnode(fs, de->inode, &ci);
                kfree(bb);
                return *out ? 0 : -ENOMEM;
            }
            off += de->rec_len;
        }
        fb++; pos += fs->block_size;
    }
    kfree(bb);
    return -ENOENT;
}

static int ext2_dir_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    ext2_vdata_t *vd = dir->fs_data;
    ext2_t *fs = vd->fs;
    ext2_inode_t di;
    int r = inode_read(fs, vd->ino, &di);
    if (r < 0) return r;
    uint8_t *bb = kmalloc(fs->block_size);
    if (!bb) return -ENOMEM;
    uint32_t ds = di.i_size, pos = 0, fb = 0;
    uint64_t cur = 0;
    while (pos < ds) {
        int32_t db = get_block_num(fs, &di, fb);
        if (db <= 0) { fb++; pos += fs->block_size; continue; }
        block_read(fs, (uint32_t)db, bb);
        uint32_t off = 0;
        while (off < fs->block_size && (pos + off) < ds) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(bb + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                bool skip = (de->name_len == 1 && de->name[0] == '.') ||
                            (de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.');
                if (!skip) {
                    if (cur == index) {
                        out->d_ino = de->inode;
                        out->d_type = (de->file_type == EXT2_FT_DIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
                        size_t n = de->name_len;
                        if (n >= VFS_MAX_NAME) n = VFS_MAX_NAME - 1;
                        memcpy(out->d_name, de->name, n);
                        out->d_name[n] = '\0';
                        kfree(bb);
                        return 0;
                    }
                    cur++;
                }
            }
            off += de->rec_len;
        }
        fb++; pos += fs->block_size;
    }
    kfree(bb);
    return -ENOENT;
}

static int ext2_dir_add_entry(ext2_t *fs, uint32_t dir_ino, uint32_t child_ino,
                              uint8_t file_type, const char *name) {
    if (!name || !name[0]) return -EINVAL;
    ext2_inode_t di;
    int r = inode_read(fs, dir_ino, &di);
    if (r < 0) return r;
    uint8_t nl = (uint8_t)strlen(name);
    uint16_t need = (uint16_t)((8 + nl + 3) & ~3);
    uint8_t *bb = kmalloc(fs->block_size);
    if (!bb) return -ENOMEM;
    uint32_t nb = (di.i_size + fs->block_size - 1) / fs->block_size;
    for (uint32_t fb = 0; fb < nb; fb++) {
        int32_t db = get_block_num(fs, &di, fb);
        if (db <= 0) continue;
        block_read(fs, (uint32_t)db, bb);
        uint32_t off = 0;
        while (off < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(bb + off);
            if (de->rec_len == 0) break;
            if (de->inode == 0 && de->rec_len >= need) {
                de->inode = child_ino;
                de->name_len = nl;
                de->file_type = file_type;
                memcpy(de->name, name, nl);
                block_write(fs, (uint32_t)db, bb);
                kfree(bb);
                return 0;
            }
            uint16_t actual = (uint16_t)((8 + de->name_len + 3) & ~3);
            uint16_t slack = de->rec_len - actual;
            if (slack >= need) {
                de->rec_len = actual;
                ext2_dir_entry_t *ne = (ext2_dir_entry_t *)(bb + off + actual);
                ne->inode = child_ino;
                ne->rec_len = slack;
                ne->name_len = nl;
                ne->file_type = file_type;
                memcpy(ne->name, name, nl);
                block_write(fs, (uint32_t)db, bb);
                kfree(bb);
                return 0;
            }
            off += de->rec_len;
        }
    }
    int32_t new_blk = alloc_block(fs);
    if (new_blk < 0) { kfree(bb); return (int)new_blk; }
    set_block_num(fs, &di, nb, (uint32_t)new_blk);
    di.i_size += fs->block_size;
    di.i_blocks += fs->block_size / 512;
    memset(bb, 0, fs->block_size);
    ext2_dir_entry_t *de = (ext2_dir_entry_t *)bb;
    de->inode = child_ino;
    de->rec_len = (uint16_t)fs->block_size;
    de->name_len = nl;
    de->file_type = file_type;
    memcpy(de->name, name, nl);
    block_write(fs, (uint32_t)new_blk, bb);
    inode_write(fs, dir_ino, &di);
    kfree(bb);
    return 0;
}

static int ext2_dir_mkdir(vnode_t *dir, const char *name, uint32_t mode) {
    if (!name || !name[0]) return -EINVAL;
    ext2_vdata_t *vd = dir->fs_data;
    ext2_t *fs = vd->fs;
    vnode_t *ex = NULL;
    if (ext2_dir_lookup(dir, name, &ex) == 0) { vnode_unref(ex); return -EEXIST; }
    int32_t ino = alloc_inode(fs);
    if (ino < 0) return (int)ino;
    ext2_inode_t ndi;
    memset(&ndi, 0, sizeof(ndi));
    ndi.i_mode = EXT2_S_IFDIR | (uint16_t)(mode ? mode : 0755);
    ndi.i_links_count = 2;
    int32_t blk = alloc_block(fs);
    if (blk < 0) { free_inode(fs, (uint32_t)ino); return (int)blk; }
    ndi.i_block[0] = (uint32_t)blk;
    ndi.i_size = fs->block_size;
    ndi.i_blocks = fs->block_size / 512;
    uint8_t *bb = kzalloc(fs->block_size);
    if (!bb) { free_block(fs, (uint32_t)blk); free_inode(fs, (uint32_t)ino); return -ENOMEM; }
    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)bb;
    dot->inode = (uint32_t)ino; dot->rec_len = 12; dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR; dot->name[0] = '.';
    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(bb + 12);
    dotdot->inode = vd->ino; dotdot->rec_len = (uint16_t)(fs->block_size - 12);
    dotdot->name_len = 2; dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.'; dotdot->name[1] = '.';
    block_write(fs, (uint32_t)blk, bb);
    kfree(bb);
    inode_write(fs, (uint32_t)ino, &ndi);
    int r = ext2_dir_add_entry(fs, vd->ino, (uint32_t)ino, EXT2_FT_DIR, name);
    if (r < 0) { free_block(fs, (uint32_t)blk); free_inode(fs, (uint32_t)ino); return r; }
    ext2_inode_t pdi;
    inode_read(fs, vd->ino, &pdi);
    pdi.i_links_count++;
    inode_write(fs, vd->ino, &pdi);
    uint32_t grp = ((uint32_t)ino - 1) / fs->sb.s_inodes_per_group;
    fs->gdt[grp].bg_used_dirs_count++;
    fs->dirty = true;
    dir->size++;
    return 0;
}

static int ext2_dir_create(vnode_t *dir, const char *name, uint32_t mode, vnode_t **out) {
    if (!name || !name[0]) return -EINVAL;
    ext2_vdata_t *vd = dir->fs_data;
    ext2_t *fs = vd->fs;
    if (ext2_dir_lookup(dir, name, out) == 0) return 0;
    int32_t ino = alloc_inode(fs);
    if (ino < 0) return (int)ino;
    ext2_inode_t ndi;
    memset(&ndi, 0, sizeof(ndi));
    ndi.i_mode = EXT2_S_IFREG | (uint16_t)(mode ? mode : 0644);
    ndi.i_links_count = 1;
    inode_write(fs, (uint32_t)ino, &ndi);
    int r = ext2_dir_add_entry(fs, vd->ino, (uint32_t)ino, EXT2_FT_REG_FILE, name);
    if (r < 0) { free_inode(fs, (uint32_t)ino); return r; }
    *out = ext2_make_vnode(fs, (uint32_t)ino, &ndi);
    if (!*out) return -ENOMEM;
    fs->dirty = true;
    dir->size++;
    return 0;
}

static int ext2_dir_unlink(vnode_t *dir, const char *name) {
    ext2_vdata_t *vd = dir->fs_data;
    ext2_t *fs = vd->fs;
    ext2_inode_t di;
    int r = inode_read(fs, vd->ino, &di);
    if (r < 0) return r;
    uint8_t *bb = kmalloc(fs->block_size);
    if (!bb) return -ENOMEM;
    size_t nl = strlen(name);
    uint32_t nblocks = (di.i_size + fs->block_size - 1) / fs->block_size;
    for (uint32_t fb = 0; fb < nblocks; fb++) {
        int32_t db = get_block_num(fs, &di, fb);
        if (db <= 0) continue;
        block_read(fs, (uint32_t)db, bb);
        uint32_t off = 0;
        ext2_dir_entry_t *prev = NULL;
        while (off < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(bb + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == nl && memcmp(de->name, name, nl) == 0) {
                uint32_t cino = de->inode;
                ext2_inode_t ci;
                inode_read(fs, cino, &ci);
                bool is_dir = ((ci.i_mode & 0xF000) == EXT2_S_IFDIR);
                free_all_blocks(fs, &ci);
                free_inode(fs, cino);
                if (prev) prev->rec_len += de->rec_len;
                else de->inode = 0;
                block_write(fs, (uint32_t)db, bb);
                if (is_dir) {
                    ext2_inode_t pdi;
                    inode_read(fs, vd->ino, &pdi);
                    if (pdi.i_links_count > 1) pdi.i_links_count--;
                    inode_write(fs, vd->ino, &pdi);
                    uint32_t grp = (cino - 1) / fs->sb.s_inodes_per_group;
                    if (fs->gdt[grp].bg_used_dirs_count > 0)
                        fs->gdt[grp].bg_used_dirs_count--;
                }
                dir->size--;
                fs->dirty = true;
                kfree(bb);
                return 0;
            }
            prev = de;
            off += de->rec_len;
        }
    }
    kfree(bb);
    return -ENOENT;
}

static int ext2_dir_rename(vnode_t *src_dir, const char *src_name,
                           vnode_t *dst_dir, const char *dst_name) {
    ext2_vdata_t *svd = src_dir->fs_data;
    ext2_t *fs = svd->fs;
    vnode_t *child = NULL;
    int r = ext2_dir_lookup(src_dir, src_name, &child);
    if (r < 0) return r;
    uint8_t ft = (child->type == VFS_NODE_DIR) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    uint32_t cino = (uint32_t)child->ino;
    vnode_unref(child);
    vnode_t *ex = NULL;
    if (ext2_dir_lookup(dst_dir, dst_name, &ex) == 0) {
        vnode_unref(ex);
        ext2_dir_unlink(dst_dir, dst_name);
    }
    ext2_vdata_t *dvd = dst_dir->fs_data;
    r = ext2_dir_add_entry(fs, dvd->ino, cino, ft, dst_name);
    if (r < 0) return r;
    ext2_inode_t sdi;
    inode_read(fs, svd->ino, &sdi);
    uint8_t *bb = kmalloc(fs->block_size);
    if (!bb) return -ENOMEM;
    size_t snl = strlen(src_name);
    uint32_t nblocks = (sdi.i_size + fs->block_size - 1) / fs->block_size;
    for (uint32_t fb = 0; fb < nblocks; fb++) {
        int32_t db = get_block_num(fs, &sdi, fb);
        if (db <= 0) continue;
        block_read(fs, (uint32_t)db, bb);
        uint32_t off = 0;
        ext2_dir_entry_t *prev = NULL;
        while (off < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(bb + off);
            if (de->rec_len == 0) break;
            if (de->inode == cino && de->name_len == snl &&
                memcmp(de->name, src_name, snl) == 0) {
                if (prev) prev->rec_len += de->rec_len;
                else de->inode = 0;
                block_write(fs, (uint32_t)db, bb);
                kfree(bb);
                fs->dirty = true;
                return 0;
            }
            prev = de;
            off += de->rec_len;
        }
    }
    kfree(bb);
    fs->dirty = true;
    return 0;
}

static const vnode_ops_t ext2_dir_ops = {
    .lookup  = ext2_dir_lookup,
    .readdir = ext2_dir_readdir,
    .mkdir   = ext2_dir_mkdir,
    .create  = ext2_dir_create,
    .unlink  = ext2_dir_unlink,
    .rename  = ext2_dir_rename,
    .stat    = ext2_stat,
    .ref     = ext2_vnode_ref,
    .unref   = ext2_vnode_unref,
};

int ext2_format(blkdev_t *dev, const char *label) {
    if (!dev) return -EINVAL;
    uint64_t disk_bytes = dev->size_bytes;
    if (disk_bytes < 64 * 1024) return -ENOSPC;
    uint32_t block_size = 1024;
    uint32_t total_blocks = (uint32_t)(disk_bytes / block_size);
    uint32_t blocks_per_group = block_size * 8;
    uint32_t inodes_per_group = (total_blocks < 8192) ? 128 : 256;
    uint32_t groups_count = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    if (groups_count == 0) groups_count = 1;
    uint32_t total_inodes = inodes_per_group * groups_count;
    uint32_t inode_size = 128;
    uint32_t it_blocks_pg = (inodes_per_group * inode_size + block_size - 1) / block_size;
    uint32_t first_data_block = 1;
    uint8_t *zb = kzalloc(block_size);
    if (!zb) return -ENOMEM;
    for (uint32_t b = 0; b < total_blocks && b < 16; b++)
        blkdev_write(dev, (uint64_t)b * block_size, zb, block_size);
    ext2_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count = total_inodes;
    sb.s_blocks_count = total_blocks;
    sb.s_r_blocks_count = total_blocks / 20;
    sb.s_free_blocks_count = total_blocks;
    sb.s_free_inodes_count = total_inodes;
    sb.s_first_data_block = first_data_block;
    sb.s_log_block_size = 0;
    sb.s_blocks_per_group = blocks_per_group;
    sb.s_frags_per_group = blocks_per_group;
    sb.s_inodes_per_group = inodes_per_group;
    sb.s_magic = EXT2_SUPER_MAGIC;
    sb.s_state = EXT2_VALID_FS;
    sb.s_errors = 1;
    sb.s_rev_level = EXT2_DYNAMIC_REV;
    sb.s_first_ino = 11;
    sb.s_inode_size = (uint16_t)inode_size;
    sb.s_max_mnt_count = 20;
    if (label) strncpy(sb.s_volume_name, label, 15);
    uint32_t gdt_block = first_data_block + 1;
    uint32_t gdt_blocks = (groups_count * sizeof(ext2_group_desc_t) + block_size - 1) / block_size;
    ext2_group_desc_t *gdt = kzalloc(gdt_blocks * block_size);
    if (!gdt) { kfree(zb); return -ENOMEM; }
    uint32_t used_total = first_data_block;
    for (uint32_t g = 0; g < groups_count; g++) {
        uint32_t gs = g * blocks_per_group + first_data_block;
        uint32_t cb = (g == 0) ? (gdt_block + gdt_blocks) : gs;
        gdt[g].bg_block_bitmap = cb++;
        gdt[g].bg_inode_bitmap = cb++;
        gdt[g].bg_inode_table = cb;
        cb += it_blocks_pg;
        uint32_t meta = cb - gs;
        if (g == 0) meta = cb - first_data_block;
        uint32_t gb = blocks_per_group;
        if (g == groups_count - 1) gb = total_blocks - gs;
        gdt[g].bg_free_blocks_count = (gb > meta) ? (uint16_t)(gb - meta) : 0;
        gdt[g].bg_free_inodes_count = (uint16_t)inodes_per_group;
        used_total += meta;
        for (uint32_t t = 0; t < it_blocks_pg; t++)
            blkdev_write(dev, (uint64_t)(gdt[g].bg_inode_table + t) * block_size, zb, block_size);
        uint8_t *bbmp = kzalloc(block_size);
        for (uint32_t i = 0; i < meta; i++) bmp_set(bbmp, i);
        if (g == 0)
            for (uint32_t i = 0; i < (gdt_block + gdt_blocks - first_data_block); i++)
                bmp_set(bbmp, i);
        for (uint32_t i = gb; i < blocks_per_group; i++) bmp_set(bbmp, i);
        blkdev_write(dev, (uint64_t)gdt[g].bg_block_bitmap * block_size, bbmp, block_size);
        kfree(bbmp);
        uint8_t *ibmp = kzalloc(block_size);
        if (g == 0) {
            for (uint32_t i = 0; i < 11; i++) { bmp_set(ibmp, i); gdt[g].bg_free_inodes_count--; }
        }
        for (uint32_t i = inodes_per_group; i < block_size * 8; i++) bmp_set(ibmp, i);
        blkdev_write(dev, (uint64_t)gdt[g].bg_inode_bitmap * block_size, ibmp, block_size);
        kfree(ibmp);
    }
    sb.s_free_blocks_count = total_blocks - used_total;
    sb.s_free_inodes_count = total_inodes - 11;
    int32_t root_blk = -1;
    {
        uint8_t *bbmp = kmalloc(block_size);
        blkdev_read(dev, (uint64_t)gdt[0].bg_block_bitmap * block_size, bbmp, block_size);
        for (uint32_t i = 0; i < blocks_per_group; i++) {
            if (!bmp_test(bbmp, i)) {
                bmp_set(bbmp, i);
                blkdev_write(dev, (uint64_t)gdt[0].bg_block_bitmap * block_size, bbmp, block_size);
                root_blk = (int32_t)(i + first_data_block);
                gdt[0].bg_free_blocks_count--;
                sb.s_free_blocks_count--;
                break;
            }
        }
        kfree(bbmp);
    }
    if (root_blk < 0) { kfree(gdt); kfree(zb); return -ENOSPC; }
    ext2_inode_t ri;
    memset(&ri, 0, sizeof(ri));
    ri.i_mode = EXT2_S_IFDIR | 0755;
    ri.i_links_count = 2;
    ri.i_block[0] = (uint32_t)root_blk;
    ri.i_size = block_size;
    ri.i_blocks = block_size / 512;
    uint64_t roff = (uint64_t)gdt[0].bg_inode_table * block_size + (uint64_t)(EXT2_ROOT_INO - 1) * inode_size;
    blkdev_write(dev, roff, &ri, sizeof(ri));
    uint8_t *rdb = kzalloc(block_size);
    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)rdb;
    dot->inode = EXT2_ROOT_INO; dot->rec_len = 12; dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR; dot->name[0] = '.';
    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(rdb + 12);
    dotdot->inode = EXT2_ROOT_INO; dotdot->rec_len = (uint16_t)(block_size - 12);
    dotdot->name_len = 2; dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.'; dotdot->name[1] = '.';
    blkdev_write(dev, (uint64_t)root_blk * block_size, rdb, block_size);
    kfree(rdb);
    gdt[0].bg_used_dirs_count = 1;
    blkdev_write(dev, EXT2_SUPER_OFFSET, &sb, sizeof(sb));
    blkdev_write(dev, (uint64_t)gdt_block * block_size, gdt, gdt_blocks * block_size);
    kfree(gdt);
    kfree(zb);
    serial_printf("[ext2] formatted: %u blocks (%u KiB), %u inodes, bs=%u, label='%s'\n",
                  total_blocks, total_blocks * block_size / 1024, total_inodes, block_size,
                  label ? label : "");
    return 0;
}

vnode_t *ext2_mount(blkdev_t *dev) {
    if (!dev) return NULL;
    ext2_t *fs = kzalloc(sizeof(ext2_t));
    if (!fs) return NULL;
    fs->dev = dev;
    if (blkdev_read(dev, EXT2_SUPER_OFFSET, &fs->sb, sizeof(fs->sb)) < 0) { kfree(fs); return NULL; }
    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) {
        serial_printf("[ext2] bad magic: 0x%x (expected 0x%x)\n", fs->sb.s_magic, EXT2_SUPER_MAGIC);
        kfree(fs);
        return NULL;
    }
    fs->block_size = 1024 << fs->sb.s_log_block_size;
    fs->inode_size = (fs->sb.s_rev_level >= EXT2_DYNAMIC_REV) ? fs->sb.s_inode_size : 128;
    fs->groups_count = (fs->sb.s_blocks_count + fs->sb.s_blocks_per_group - 1) / fs->sb.s_blocks_per_group;
    fs->inodes_per_block = fs->block_size / fs->inode_size;
    fs->ptrs_per_block = fs->block_size / 4;
    uint32_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    uint32_t gdt_sz = fs->groups_count * sizeof(ext2_group_desc_t);
    uint32_t gdt_blocks = (gdt_sz + fs->block_size - 1) / fs->block_size;
    fs->gdt = kmalloc(gdt_blocks * fs->block_size);
    if (!fs->gdt) { kfree(fs); return NULL; }
    if (blkdev_read(dev, (uint64_t)gdt_block * fs->block_size, fs->gdt, gdt_blocks * fs->block_size) < 0) {
        kfree(fs->gdt); kfree(fs); return NULL;
    }
    ext2_inode_t root_di;
    if (inode_read(fs, EXT2_ROOT_INO, &root_di) < 0) { kfree(fs->gdt); kfree(fs); return NULL; }
    vnode_t *root = ext2_make_vnode(fs, EXT2_ROOT_INO, &root_di);
    if (!root) { kfree(fs->gdt); kfree(fs); return NULL; }
    serial_printf("[ext2] mounted '%s': %u blocks (%u free), %u inodes (%u free), bs=%u\n",
                  fs->sb.s_volume_name, fs->sb.s_blocks_count, fs->sb.s_free_blocks_count,
                  fs->sb.s_inodes_count, fs->sb.s_free_inodes_count, fs->block_size);
    return root;
}

void ext2_sync(ext2_t *fs) {
    if (!fs || !fs->dirty) return;
    sb_flush(fs);
    gdt_flush(fs);
    if (fs->dev->ops && fs->dev->ops->flush) fs->dev->ops->flush(fs->dev);
    fs->dirty = false;
}

void ext2_unmount(ext2_t *fs) {
    if (!fs) return;
    ext2_sync(fs);
    if (fs->gdt) kfree(fs->gdt);
    kfree(fs);
}