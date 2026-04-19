#include "../../include/fs/fat32.h"
#include "../../include/fs/vfs.h"
#include "../../include/drivers/blkdev.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/errno.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CLUSTER_BYTES 32768

static int read_sector(fat32_t *fs, uint32_t lba, void *buf) {
    return fs->dev->ops->read_sectors(fs->dev, lba, 1, buf);
}
static int write_sector(fat32_t *fs, uint32_t lba, const void *buf) {
    if (fs->readonly) return -EROFS;
    return fs->dev->ops->write_sectors(fs->dev, lba, 1, buf);
}

static uint32_t cluster_to_lba(fat32_t *fs, uint32_t cluster) {
    return fs->first_data_sector + (cluster - 2) * fs->sectors_per_cluster;
}

static uint32_t fat_read_entry(fat32_t *fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->first_fat_sector + (fat_offset / fs->bytes_per_sector);
    uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

    uint8_t sector[FAT32_SECTOR_SIZE];
    if (read_sector(fs, fat_sector, sector) < 0) return FAT32_EOC;

    uint32_t val;
    memcpy(&val, sector + ent_offset, 4);
    return val & FAT32_CLUSTER_MASK;
}

static int fat_write_entry(fat32_t *fs, uint32_t cluster, uint32_t value) {
    if (fs->readonly) return -EROFS;
    uint32_t fat_offset = cluster * 4;
    uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

    for (uint32_t i = 0; i < fs->num_fats; i++) {
        uint32_t fat_sector = fs->first_fat_sector + i * fs->fat_size_sectors
                              + (fat_offset / fs->bytes_per_sector);
        uint8_t sector[FAT32_SECTOR_SIZE];
        int r = read_sector(fs, fat_sector, sector);
        if (r < 0) return r;
        uint32_t existing;
        memcpy(&existing, sector + ent_offset, 4);
        uint32_t reserved = existing & 0xF0000000;
        uint32_t merged = (value & FAT32_CLUSTER_MASK) | reserved;
        memcpy(sector + ent_offset, &merged, 4);
        r = write_sector(fs, fat_sector, sector);
        if (r < 0) return r;
    }
    fs->dirty = true;
    return 0;
}

static bool fat32_is_eoc(uint32_t cluster) {
    return (cluster & FAT32_CLUSTER_MASK) >= FAT32_EOC_MIN;
}

static int read_cluster(fat32_t *fs, uint32_t cluster, void *buf) {
    uint32_t lba = cluster_to_lba(fs, cluster);
    return fs->dev->ops->read_sectors(fs->dev, lba, fs->sectors_per_cluster, buf);
}
static int write_cluster(fat32_t *fs, uint32_t cluster, const void *buf) {
    if (fs->readonly) return -EROFS;
    uint32_t lba = cluster_to_lba(fs, cluster);
    return fs->dev->ops->write_sectors(fs->dev, lba, fs->sectors_per_cluster, buf);
}

static uint32_t allocate_cluster(fat32_t *fs) {
    if (fs->readonly) return 0;
    uint32_t start = fs->next_free_hint;
    if (start < 2) start = 2;
    for (uint32_t i = 0; i < fs->total_clusters; i++) {
        uint32_t c = 2 + ((start - 2 + i) % fs->total_clusters);
        uint32_t v = fat_read_entry(fs, c);
        if (v == FAT32_FREE_CLUSTER) {
            if (fat_write_entry(fs, c, FAT32_EOC) < 0) return 0;
            fs->next_free_hint = c + 1;
            if (fs->free_count != 0xFFFFFFFF) fs->free_count--;

            uint8_t *zero = (uint8_t *)malloc(fs->bytes_per_cluster);
            if (zero) {
                memset(zero, 0, fs->bytes_per_cluster);
                write_cluster(fs, c, zero);
                free(zero);
            }
            return c;
        }
    }
    return 0;
}

static int free_cluster_chain(fat32_t *fs, uint32_t first) {
    if (fs->readonly) return -EROFS;
    uint32_t c = first;
    while (c >= 2 && !fat32_is_eoc(c)) {
        uint32_t next = fat_read_entry(fs, c);
        fat_write_entry(fs, c, FAT32_FREE_CLUSTER);
        if (fs->free_count != 0xFFFFFFFF) fs->free_count++;
        c = next;
    }
    return 0;
}

int fat32_sync(fat32_t *fs) {
    if (!fs || fs->readonly) return 0;
    if (fs->fsinfo_sector) {
        uint8_t sec[FAT32_SECTOR_SIZE];
        if (read_sector(fs, fs->fsinfo_sector, sec) == 0) {
            fat32_fsinfo_t *fsi = (fat32_fsinfo_t *)sec;
            if (fsi->lead_sig == FAT32_FSINFO_LEAD_SIG
                && fsi->struct_sig == FAT32_FSINFO_STRUCT_SIG) {
                fsi->free_count = fs->free_count;
                fsi->next_free  = fs->next_free_hint;
                write_sector(fs, fs->fsinfo_sector, sec);
            }
        }
    }
    if (fs->dev->ops->flush) fs->dev->ops->flush(fs->dev);
    fs->dirty = false;
    return 0;
}

static void fat_name_to_string(const uint8_t raw[11], char *out) {
    int o = 0;
    for (int i = 0; i < 8; i++) {
        if (raw[i] == ' ') break;
        out[o++] = raw[i];
    }
    if (raw[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11; i++) {
            if (raw[i] == ' ') break;
            out[o++] = raw[i];
        }
    }
    out[o] = '\0';
    for (int i = 0; out[i]; i++)
        if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
}

static void string_to_fat_name(const char *name, uint8_t out[11]) {
    memset(out, ' ', 11);
    const char *dot = NULL;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    int base_len = dot ? (int)(dot - name) : (int)strlen(name);
    if (base_len > 8) base_len = 8;
    for (int i = 0; i < base_len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[i] = (uint8_t)c;
    }
    if (dot) {
        const char *ext = dot + 1;
        int el = (int)strlen(ext);
        if (el > 3) el = 3;
        for (int i = 0; i < el; i++) {
            char c = ext[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[8 + i] = (uint8_t)c;
        }
    }
}
static uint8_t lfn_checksum(const uint8_t sfn[11]) {
    uint8_t s = 0;
    for (int i = 0; i < 11; i++) s = (uint8_t)(((s & 1) << 7) + (s >> 1) + sfn[i]);
    return s;
}

static void lfn_append(char *out, int *out_len, int out_cap, const uint8_t *src, int count) {
    for (int i = 0; i < count; i++) {
        uint16_t wc = (uint16_t)(src[i*2] | (src[i*2+1] << 8));
        if (wc == 0x0000 || wc == 0xFFFF) return;
        if (*out_len + 1 >= out_cap) return;
        out[(*out_len)++] = (wc < 128) ? (char)wc : '?';
    }
    out[*out_len] = '\0';
}

static vnode_t *fat32_alloc_vnode(fat32_t *fs, uint32_t first_cluster,
                                  uint32_t size, uint8_t attr,
                                  uint32_t dir_cluster, uint32_t dir_entry_off);
static const vnode_ops_t fat32_file_ops;
static const vnode_ops_t fat32_dir_ops;

static int fat32_traverse_dir(fat32_t *fs, uint32_t start_cluster,
                              int (*cb)(fat32_t *fs, uint32_t cluster,
                                        uint32_t entry_off,
                                        fat32_dirent_t *e,
                                        const char *name, void *ud),
                              void *ud)
{
    uint32_t cluster = start_cluster ? start_cluster : fs->root_cluster;
    char lfn_buf[260];
    int  lfn_len = 0;
    lfn_buf[0] = '\0';

    uint8_t *cluster_buf = fs->shared_buf;
    while (!fat32_is_eoc(cluster) && cluster >= 2) {
        if (read_cluster(fs, cluster, cluster_buf) < 0) return -EIO;

        uint32_t entries = fs->bytes_per_cluster / 32;
        for (uint32_t i = 0; i < entries; i++) {
            fat32_dirent_t *e = (fat32_dirent_t *)(cluster_buf + i * 32);
            if (e->name[0] == 0x00) return -ENOENT;
            if ((uint8_t)e->name[0] == 0xE5) { lfn_len = 0; lfn_buf[0] = '\0'; continue; }

            if (e->attr == FAT_ATTR_LONG_NAME) {
                fat32_lfn_t *lfn = (fat32_lfn_t *)e;
                char part[14]; int pl = 0;
                lfn_append(part, &pl, sizeof(part), lfn->name1, 5);
                lfn_append(part, &pl, sizeof(part), lfn->name2, 6);
                lfn_append(part, &pl, sizeof(part), lfn->name3, 2);
                char merged[260];
                int ml = 0;
                for (int k = 0; k < pl && ml < 259; k++) merged[ml++] = part[k];
                for (int k = 0; k < lfn_len && ml < 259; k++) merged[ml++] = lfn_buf[k];
                merged[ml] = '\0';
                memcpy(lfn_buf, merged, ml + 1);
                lfn_len = ml;
                continue;
            }

            if (e->attr & FAT_ATTR_VOLUME_ID) { lfn_len = 0; lfn_buf[0] = '\0'; continue; }

            char sfn[13];
            fat_name_to_string(e->name, sfn);
            const char *name = (lfn_len > 0) ? lfn_buf : sfn;
            int r = cb(fs, cluster, i * 32, e, name, ud);
            if (r != 0) return r;
            lfn_len = 0; lfn_buf[0] = '\0';
        }
        cluster = fat_read_entry(fs, cluster);
    }
    return -ENOENT;
}

typedef struct {
    const char *target;
    vnode_t   **out;
    uint32_t    found;
} lookup_ctx_t;

static int lookup_cb(fat32_t *fs, uint32_t cluster, uint32_t entry_off,
                     fat32_dirent_t *e, const char *name, void *ud)
{
    lookup_ctx_t *ctx = (lookup_ctx_t *)ud;
    bool match = true;
    for (int k = 0; ctx->target[k] || name[k]; k++) {
        char a = ctx->target[k];
        char b = name[k];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) { match = false; break; }
    }
    if (match) {
        uint32_t first = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
        vnode_t *vn = fat32_alloc_vnode(fs, first, e->file_size, e->attr,
                                        cluster, entry_off);
        if (!vn) return -ENOMEM;
        *ctx->out = vn;
        ctx->found = 1;
        return 1;
    }
    return 0;
}

static int fat32_dir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    fat32_vdata_t *vd = (fat32_vdata_t *)dir->fs_data;
    if (!vd || !vd->fs) return -EIO;
    lookup_ctx_t ctx = { .target = name, .out = out, .found = 0 };
    int r = fat32_traverse_dir(vd->fs, vd->first_cluster, lookup_cb, &ctx);
    if (ctx.found) return 0;
    return (r == -ENOENT) ? -ENOENT : r;
}

typedef struct {
    uint64_t wanted;
    uint64_t cur;
    vfs_dirent_t *out;
    int got;
} readdir_ctx_t;

static int readdir_cb(fat32_t *fs, uint32_t cluster, uint32_t entry_off,
                      fat32_dirent_t *e, const char *name, void *ud)
{
    (void)fs; (void)cluster; (void)entry_off;
    readdir_ctx_t *ctx = (readdir_ctx_t *)ud;
    if (ctx->cur == ctx->wanted) {
        memset(ctx->out, 0, sizeof(*ctx->out));
        int n = 0;
        while (name[n] && n < (int)sizeof(ctx->out->d_name) - 1) {
            ctx->out->d_name[n] = name[n]; n++;
        }
        ctx->out->d_name[n] = '\0';
        ctx->out->d_type = (e->attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
        ctx->got = 1;
        return 1;
    }
    ctx->cur++;
    return 0;
}

static int fat32_dir_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    fat32_vdata_t *vd = (fat32_vdata_t *)dir->fs_data;
    if (!vd || !vd->fs) return -EIO;
    readdir_ctx_t ctx = { .wanted = index, .cur = 0, .out = out, .got = 0 };
    int r = fat32_traverse_dir(vd->fs, vd->first_cluster, readdir_cb, &ctx);
    if (ctx.got) return 0;
    return (r == -ENOENT) ? -ENOENT : r;
}

static int64_t fat32_file_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    fat32_vdata_t *vd = (fat32_vdata_t *)node->fs_data;
    if (!vd || !vd->fs) return -EIO;
    if (offset >= vd->file_size) return 0;
    if (offset + len > vd->file_size) len = vd->file_size - offset;

    fat32_t *fs = vd->fs;
    uint32_t cluster = vd->first_cluster;
    uint32_t cluster_off = (uint32_t)(offset / fs->bytes_per_cluster);
    for (uint32_t i = 0; i < cluster_off; i++) {
        if (fat32_is_eoc(cluster) || cluster < 2) return 0;
        cluster = fat_read_entry(fs, cluster);
    }

    size_t done = 0;
    uint32_t in_cluster_off = (uint32_t)(offset % fs->bytes_per_cluster);
    uint8_t *cluster_buf = fs->shared_buf;
    if (!cluster_buf) return -EIO;

    while (done < len && !fat32_is_eoc(cluster) && cluster >= 2) {
        if (read_cluster(fs, cluster, cluster_buf) < 0) return -EIO;
        uint32_t avail = fs->bytes_per_cluster - in_cluster_off;
        uint32_t take = (avail < (len - done)) ? avail : (uint32_t)(len - done);
        memcpy((uint8_t *)buf + done, cluster_buf + in_cluster_off, take);
        done += take;
        in_cluster_off = 0;
        if (done < len) cluster = fat_read_entry(fs, cluster);
    }
    return (int64_t)done;
}

static int64_t fat32_file_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    fat32_vdata_t *vd = (fat32_vdata_t *)node->fs_data;
    if (!vd || !vd->fs) return -EIO;
    if (vd->fs->readonly) return -EROFS;
    fat32_t *fs = vd->fs;

    if (vd->first_cluster == 0) {
        uint32_t c = allocate_cluster(fs);
        if (c == 0) return -ENOSPC;
        vd->first_cluster = c;
        vd->cached_cluster = c;
        vd->cached_index = 0;
    }

    uint32_t target_index = (uint32_t)(offset / fs->bytes_per_cluster);
    uint32_t cluster;
    uint32_t start_index;

    if (vd->cached_cluster != 0 && vd->cached_index <= target_index) {
        cluster = vd->cached_cluster;
        start_index = vd->cached_index;
    } else {
        cluster = vd->first_cluster;
        start_index = 0;
    }

    for (uint32_t i = start_index; i < target_index; i++) {
        uint32_t next = fat_read_entry(fs, cluster);
        if (fat32_is_eoc(next) || next < 2) {
            uint32_t nc = allocate_cluster(fs);
            if (nc == 0) return -ENOSPC;
            fat_write_entry(fs, cluster, nc);
            cluster = nc;
        } else {
            cluster = next;
        }
    }

    size_t done = 0;
    uint32_t in_cluster_off = (uint32_t)(offset % fs->bytes_per_cluster);
    uint32_t current_index = target_index;

    if (!vd->io_buf) {
        vd->io_buf = (uint8_t *)malloc(fs->bytes_per_cluster);
        if (!vd->io_buf) return -ENOMEM;
    }

    while (done < len) {
        uint32_t avail = fs->bytes_per_cluster - in_cluster_off;
        uint32_t take = (avail < (len - done)) ? avail : (uint32_t)(len - done);

        if (in_cluster_off == 0 && take == fs->bytes_per_cluster) {
            if (fs->dev->ops->write_sectors(fs->dev,
                    cluster_to_lba(fs, cluster),
                    fs->sectors_per_cluster,
                    (const uint8_t *)buf + done) < 0) return -EIO;
        } else {
            if (read_cluster(fs, cluster, vd->io_buf) < 0) return -EIO;
            memcpy(vd->io_buf + in_cluster_off, (const uint8_t *)buf + done, take);
            if (write_cluster(fs, cluster, vd->io_buf) < 0) return -EIO;
        }

        done += take;
        in_cluster_off = 0;

        if (done < len) {
            uint32_t next = fat_read_entry(fs, cluster);
            if (fat32_is_eoc(next) || next < 2) {
                uint32_t nc = allocate_cluster(fs);
                if (nc == 0) break;
                fat_write_entry(fs, cluster, nc);
                next = nc;
            }
            cluster = next;
            current_index++;
        }
    }

    vd->cached_cluster = cluster;
    vd->cached_index   = current_index;

    uint64_t new_size = offset + done;
    if (new_size > vd->file_size) {
        vd->file_size = (uint32_t)new_size;
        node->size = new_size;
        vd->size_dirty = true;
    }

    return (int64_t)done;
}

static int fat32_file_stat(vnode_t *node, vfs_stat_t *out) {
    fat32_vdata_t *vd = (fat32_vdata_t *)node->fs_data;
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = node->type;
    out->st_mode = 0644;
    out->st_size = vd ? vd->file_size : 0;
    return 0;
}

static void fat32_common_ref(vnode_t *n) { n->refcount++; }
static void fat32_common_unref(vnode_t *n) {
    if (--n->refcount <= 0) {
        fat32_vdata_t *vd = (fat32_vdata_t *)n->fs_data;
        if (vd && vd->fs && vd->size_dirty && n->type == VFS_NODE_FILE) {
            uint8_t *tmp = (uint8_t *)malloc(vd->fs->bytes_per_cluster);
            if (tmp) {
                if (read_cluster(vd->fs, vd->dir_cluster, tmp) == 0) {
                    fat32_dirent_t ent;
                    memcpy(&ent, tmp + vd->dir_entry_offset, sizeof(ent));
                    ent.file_size  = vd->file_size;
                    ent.cluster_lo = (uint16_t)(vd->first_cluster & 0xFFFF);
                    ent.cluster_hi = (uint16_t)((vd->first_cluster >> 16) & 0xFFFF);
                    ent.attr       = vd->attr ? vd->attr : FAT_ATTR_ARCHIVE;
                    memcpy(tmp + vd->dir_entry_offset, &ent, sizeof(ent));
                    write_cluster(vd->fs, vd->dir_cluster, tmp);
                }
                free(tmp);
            }
            fat32_sync(vd->fs);
        }
        if (vd) {
            if (vd->io_buf) { free(vd->io_buf); vd->io_buf = NULL; }
            free(vd);
            n->fs_data = NULL;
        }
        free(n);
    }
}

static bool name_fits_sfn(const char *name) {
    int dot = -1;
    int len = 0;
    for (int i = 0; name[i]; i++) len++;
    if (len > 12) return false;
    for (int i = 0; i < len; i++) {
        if (name[i] == '.') {
            if (dot != -1) return false;
            dot = i;
        }
    }
    int base_len = (dot == -1) ? len : dot;
    int ext_len  = (dot == -1) ? 0   : (len - dot - 1);
    if (base_len == 0 || base_len > 8) return false;
    if (ext_len > 3) return false;
    for (int i = 0; i < len; i++) {
        char c = name[i];
        if (c == '.') continue;
        if (c >= 'a' && c <= 'z') return false;
        if (c == ' ') return false;
    }
    return true;
}

static void generate_short_name(const char *lfn, uint8_t out[11]) {
    memset(out, ' ', 11);
    int oi = 0;
    int i = 0;
    int last_dot = -1;
    for (int k = 0; lfn[k]; k++) if (lfn[k] == '.') last_dot = k;

    int base_end = (last_dot >= 0) ? last_dot : -1;
    while (lfn[i] && (base_end < 0 || i < base_end) && oi < 6) {
        char c = lfn[i++];
        if (c == ' ' || c == '.') continue;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) c = '_';
        out[oi++] = (uint8_t)c;
    }
    out[oi++] = '~';
    out[oi++] = '1';

    if (last_dot >= 0) {
        int ei = 8;
        int k = last_dot + 1;
        while (lfn[k] && ei < 11) {
            char c = lfn[k++];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-')) c = '_';
            out[ei++] = (uint8_t)c;
        }
    }
}

static int find_free_slots(fat32_t *fs, uint32_t dir_cluster, uint32_t count,
                           uint32_t *out_cluster, uint32_t *out_off)
{
    uint32_t cluster = dir_cluster ? dir_cluster : fs->root_cluster;
    uint8_t *cluster_buf = fs->shared_buf;
    uint32_t run_cluster = 0, run_off = 0, run_len = 0;

    while (true) {
        if (read_cluster(fs, cluster, cluster_buf) < 0) return -EIO;
        uint32_t entries = fs->bytes_per_cluster / 32;
        for (uint32_t i = 0; i < entries; i++) {
            uint8_t first = cluster_buf[i * 32];
            if (first == 0x00 || first == 0xE5) {
                if (run_len == 0) { run_cluster = cluster; run_off = i * 32; }
                run_len++;
                if (run_len >= count) {
                    *out_cluster = run_cluster;
                    *out_off     = run_off;
                    return 0;
                }
            } else {
                run_len = 0;
            }
        }
        uint32_t next = fat_read_entry(fs, cluster);
        if (fat32_is_eoc(next) || next < 2) {
            uint32_t nc = allocate_cluster(fs);
            if (nc == 0) return -ENOSPC;
            fat_write_entry(fs, cluster, nc);
            if (run_len == 0) {
                *out_cluster = nc;
                *out_off     = 0;
            } else {
                *out_cluster = run_cluster;
                *out_off     = run_off;
            }
            return 0;
        }
        cluster = next;
    }
}

static void lfn_put_chars(uint8_t *slot_bytes, const char *src_utf8, int src_len, int offset) {
    static const uint8_t positions1[5] = { 1, 3, 5, 7, 9 };
    static const uint8_t positions2[6] = { 14, 16, 18, 20, 22, 24 };
    static const uint8_t positions3[2] = { 28, 30 };

    for (int i = 0; i < 5; i++) {
        int idx = offset + i;
        uint16_t ch;
        if (idx < src_len) ch = (uint8_t)src_utf8[idx];
        else if (idx == src_len) ch = 0;
        else ch = 0xFFFF;
        slot_bytes[positions1[i]]     = (uint8_t)(ch & 0xFF);
        slot_bytes[positions1[i] + 1] = (uint8_t)(ch >> 8);
    }
    for (int i = 0; i < 6; i++) {
        int idx = offset + 5 + i;
        uint16_t ch;
        if (idx < src_len) ch = (uint8_t)src_utf8[idx];
        else if (idx == src_len) ch = 0;
        else ch = 0xFFFF;
        slot_bytes[positions2[i]]     = (uint8_t)(ch & 0xFF);
        slot_bytes[positions2[i] + 1] = (uint8_t)(ch >> 8);
    }
    for (int i = 0; i < 2; i++) {
        int idx = offset + 11 + i;
        uint16_t ch;
        if (idx < src_len) ch = (uint8_t)src_utf8[idx];
        else if (idx == src_len) ch = 0;
        else ch = 0xFFFF;
        slot_bytes[positions3[i]]     = (uint8_t)(ch & 0xFF);
        slot_bytes[positions3[i] + 1] = (uint8_t)(ch >> 8);
    }
}

static int write_dirents_with_lfn(fat32_t *fs, uint32_t host_cluster, uint32_t host_off,
                                  const char *name, const fat32_dirent_t *sfn_entry,
                                  int slot_count)
{
    uint8_t *buf = fs->shared_buf;
    if (read_cluster(fs, host_cluster, buf) < 0) return -EIO;

    int name_len = 0;
    while (name[name_len]) name_len++;

    uint8_t csum = lfn_checksum(sfn_entry->name);

    int lfn_count = slot_count - 1;
    uint32_t off = host_off;

    for (int i = 0; i < lfn_count; i++) {
        int seq      = lfn_count - i;
        int char_off = (seq - 1) * 13;
        uint8_t entry[32];
        memset(entry, 0, 32);
        uint8_t ord = (uint8_t)seq;
        if (i == 0) ord |= 0x40;
        entry[0]  = ord;
        entry[11] = 0x0F;
        entry[12] = 0;
        entry[13] = csum;
        entry[26] = 0;
        entry[27] = 0;
        lfn_put_chars(entry, name, name_len, char_off);

        if (off >= fs->bytes_per_cluster) {
            if (write_cluster(fs, host_cluster, buf) < 0) return -EIO;
            uint32_t next = fat_read_entry(fs, host_cluster);
            if (fat32_is_eoc(next) || next < 2) {
                uint32_t nc = allocate_cluster(fs);
                if (nc == 0) return -ENOSPC;
                fat_write_entry(fs, host_cluster, nc);
                next = nc;
            }
            host_cluster = next;
            if (read_cluster(fs, host_cluster, buf) < 0) return -EIO;
            off = 0;
        }
        memcpy(buf + off, entry, 32);
        off += 32;
    }

    if (off >= fs->bytes_per_cluster) {
        if (write_cluster(fs, host_cluster, buf) < 0) return -EIO;
        uint32_t next = fat_read_entry(fs, host_cluster);
        if (fat32_is_eoc(next) || next < 2) {
            uint32_t nc = allocate_cluster(fs);
            if (nc == 0) return -ENOSPC;
            fat_write_entry(fs, host_cluster, nc);
            next = nc;
        }
        host_cluster = next;
        if (read_cluster(fs, host_cluster, buf) < 0) return -EIO;
        off = 0;
    }
    memcpy(buf + off, sfn_entry, 32);
    if (write_cluster(fs, host_cluster, buf) < 0) return -EIO;

    return 0;
}

static int fat32_dir_create(vnode_t *dir, const char *name, uint32_t mode,
                            vnode_t **out)
{
    (void)mode;
    fat32_vdata_t *vd = (fat32_vdata_t *)dir->fs_data;
    if (!vd || !vd->fs) return -EIO;
    if (vd->fs->readonly) return -EROFS;
    fat32_t *fs = vd->fs;

    vnode_t *tmp = NULL;
    if (fat32_dir_lookup(dir, name, &tmp) == 0) {
        if (out) *out = tmp;
        else fat32_common_unref(tmp);
        return -EEXIST;
    }

    int name_len = 0;
    while (name[name_len]) name_len++;

    bool use_lfn = !name_fits_sfn(name);
    int lfn_entries = 0;
    if (use_lfn) {
        lfn_entries = (name_len + 12) / 13;
    }
    int slot_count = lfn_entries + 1;

    uint32_t parent_cluster = vd->first_cluster ? vd->first_cluster : fs->root_cluster;
    uint32_t host_cluster, host_off;
    int r = find_free_slots(fs, parent_cluster, slot_count, &host_cluster, &host_off);
    if (r < 0) return r;

    fat32_dirent_t e;
    memset(&e, 0, sizeof(e));
    if (use_lfn) generate_short_name(name, e.name);
    else         string_to_fat_name(name, e.name);
    e.attr = FAT_ATTR_ARCHIVE;
    e.cluster_lo = 0;
    e.cluster_hi = 0;
    e.file_size = 0;

    if (use_lfn) {
        r = write_dirents_with_lfn(fs, host_cluster, host_off, name, &e, slot_count);
        if (r < 0) return r;
    } else {
        uint8_t *cluster_buf = fs->shared_buf;
        if (read_cluster(fs, host_cluster, cluster_buf) < 0) return -EIO;
        memcpy(cluster_buf + host_off, &e, sizeof(e));
        if (write_cluster(fs, host_cluster, cluster_buf) < 0) return -EIO;
    }

    if (out) {
        uint32_t sfn_cluster = host_cluster;
        uint32_t sfn_off = host_off + lfn_entries * 32;
        if (sfn_off >= fs->bytes_per_cluster) {
            uint32_t next = fat_read_entry(fs, host_cluster);
            sfn_cluster = next;
            sfn_off = sfn_off - fs->bytes_per_cluster;
        }
        vnode_t *vn = fat32_alloc_vnode(fs, 0, 0, FAT_ATTR_ARCHIVE,
                                        sfn_cluster, sfn_off);
        if (!vn) return -ENOMEM;
        *out = vn;
    }
    fat32_sync(fs);
    return 0;
}

static int fat32_dir_mkdir(vnode_t *dir, const char *name, uint32_t mode) {
    (void)mode;
    fat32_vdata_t *vd = (fat32_vdata_t *)dir->fs_data;
    if (!vd || !vd->fs) return -EIO;
    if (vd->fs->readonly) return -EROFS;
    fat32_t *fs = vd->fs;

    vnode_t *tmp = NULL;
    if (fat32_dir_lookup(dir, name, &tmp) == 0) {
        fat32_common_unref(tmp);
        return -EEXIST;
    }

    uint32_t new_cluster = allocate_cluster(fs);
    if (new_cluster == 0) return -ENOSPC;

    uint32_t parent_cluster = vd->first_cluster ? vd->first_cluster : fs->root_cluster;
    uint8_t *dir_buf = fs->shared_buf;
    memset(dir_buf, 0, fs->bytes_per_cluster);
    fat32_dirent_t dot, dotdot;
    memset(&dot, 0, sizeof(dot));
    memset(&dotdot, 0, sizeof(dotdot));
    memcpy(dot.name, ".          ", 11);
    dot.attr = FAT_ATTR_DIRECTORY;
    dot.cluster_lo = (uint16_t)(new_cluster & 0xFFFF);
    dot.cluster_hi = (uint16_t)((new_cluster >> 16) & 0xFFFF);
    memcpy(dotdot.name, "..         ", 11);
    dotdot.attr = FAT_ATTR_DIRECTORY;
    uint32_t parent_ref = (parent_cluster == fs->root_cluster) ? 0 : parent_cluster;
    dotdot.cluster_lo = (uint16_t)(parent_ref & 0xFFFF);
    dotdot.cluster_hi = (uint16_t)((parent_ref >> 16) & 0xFFFF);
    memcpy(dir_buf, &dot, 32);
    memcpy(dir_buf + 32, &dotdot, 32);
    if (write_cluster(fs, new_cluster, dir_buf) < 0) {
        free_cluster_chain(fs, new_cluster);
        return -EIO;
    }

    int name_len = 0;
    while (name[name_len]) name_len++;
    bool use_lfn = !name_fits_sfn(name);
    int lfn_entries = use_lfn ? (name_len + 12) / 13 : 0;
    int slot_count = lfn_entries + 1;

    uint32_t host_cluster, host_off;
    int r = find_free_slots(fs, parent_cluster, slot_count, &host_cluster, &host_off);
    if (r < 0) { free_cluster_chain(fs, new_cluster); return r; }

    fat32_dirent_t e;
    memset(&e, 0, sizeof(e));
    if (use_lfn) generate_short_name(name, e.name);
    else         string_to_fat_name(name, e.name);
    e.attr = FAT_ATTR_DIRECTORY;
    e.cluster_lo = (uint16_t)(new_cluster & 0xFFFF);
    e.cluster_hi = (uint16_t)((new_cluster >> 16) & 0xFFFF);
    e.file_size = 0;

    if (use_lfn) {
        r = write_dirents_with_lfn(fs, host_cluster, host_off, name, &e, slot_count);
        if (r < 0) { free_cluster_chain(fs, new_cluster); return r; }
    } else {
        uint8_t *buf2 = fs->shared_buf;
        if (read_cluster(fs, host_cluster, buf2) < 0) {
            free_cluster_chain(fs, new_cluster);
            return -EIO;
        }
        memcpy(buf2 + host_off, &e, sizeof(e));
        if (write_cluster(fs, host_cluster, buf2) < 0) {
            free_cluster_chain(fs, new_cluster);
            return -EIO;
        }
    }

    fat32_sync(fs);
    return 0;
}

typedef struct {
    const char *target;
    int         done;
} unlink_ctx_t;

static int unlink_cb(fat32_t *fs, uint32_t cluster, uint32_t entry_off,
                     fat32_dirent_t *e, const char *name, void *ud)
{
    unlink_ctx_t *ctx = (unlink_ctx_t *)ud;
    bool match = true;
    for (int k = 0; ctx->target[k] || name[k]; k++) {
        char a = ctx->target[k];
        char b = name[k];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) { match = false; break; }
    }
    if (!match) return 0;

    uint32_t first = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
    if (first != 0) free_cluster_chain(fs, first);

    uint8_t *buf = fs->shared_buf;
    if (read_cluster(fs, cluster, buf) < 0) return -EIO;
    buf[entry_off] = 0xE5;
    if (write_cluster(fs, cluster, buf) < 0) return -EIO;
    ctx->done = 1;
    return 1;
}

static int fat32_dir_unlink(vnode_t *dir, const char *name) {
    fat32_vdata_t *vd = (fat32_vdata_t *)dir->fs_data;
    if (!vd || !vd->fs) return -EIO;
    if (vd->fs->readonly) return -EROFS;
    unlink_ctx_t ctx = { .target = name, .done = 0 };
    fat32_traverse_dir(vd->fs, vd->first_cluster, unlink_cb, &ctx);
    if (!ctx.done) return -ENOENT;
    fat32_sync(vd->fs);
    return 0;
}

static int fat32_dir_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_DIR;
    out->st_mode = 0755;
    out->st_size = 0;
    return 0;
}

static const vnode_ops_t fat32_file_ops = {
    .read  = fat32_file_read,
    .write = fat32_file_write,
    .stat  = fat32_file_stat,
    .ref   = fat32_common_ref,
    .unref = fat32_common_unref,
};

static const vnode_ops_t fat32_dir_ops = {
    .lookup  = fat32_dir_lookup,
    .readdir = fat32_dir_readdir,
    .create  = fat32_dir_create,
    .mkdir   = fat32_dir_mkdir,
    .unlink  = fat32_dir_unlink,
    .stat    = fat32_dir_stat,
    .ref     = fat32_common_ref,
    .unref   = fat32_common_unref,
};

static uint64_t g_fat32_ino = 1000;

static vnode_t *fat32_alloc_vnode(fat32_t *fs, uint32_t first_cluster,
                                  uint32_t size, uint8_t attr,
                                  uint32_t dir_cluster, uint32_t dir_entry_off)
{
    vnode_t *vn = calloc(1, sizeof(vnode_t));
    if (!vn) return NULL;
    fat32_vdata_t *vd = calloc(1, sizeof(fat32_vdata_t));
    if (!vd) { free(vn); return NULL; }

    vd->fs               = fs;
    vd->first_cluster    = first_cluster;
    vd->file_size        = size;
    vd->attr             = attr;
    vd->dir_cluster      = dir_cluster;
    vd->dir_entry_offset = dir_entry_off;
    vd->cached_cluster   = first_cluster;
    vd->cached_index     = 0;
    vd->size_dirty       = false;

    vn->ino      = g_fat32_ino++;
    vn->refcount = 1;
    vn->fs_data  = vd;

    if (attr & FAT_ATTR_DIRECTORY) {
        vn->type = VFS_NODE_DIR;
        vn->mode = 0755;
        vn->ops  = &fat32_dir_ops;
    } else {
        vn->type = VFS_NODE_FILE;
        vn->mode = 0644;
        vn->size = size;
        vn->ops  = &fat32_file_ops;
    }
    return vn;
}

vnode_t *fat32_mount(blkdev_t *dev) {
    if (!dev) return NULL;

    uint8_t sec[FAT32_SECTOR_SIZE];
    if (dev->ops->read_sectors(dev, 0, 1, sec) < 0) {
        serial_printf("[FAT32] cannot read BPB on %s\n", dev->name);
        return NULL;
    }

    fat32_bpb_t *bpb = (fat32_bpb_t *)sec;
    if (bpb->bs_signature != 0xAA55) {
        serial_printf("[FAT32] %s: no boot signature\n", dev->name);
        return NULL;
    }
    if (bpb->bpb_bytes_per_sector != 512) {
        serial_printf("[FAT32] %s: unsupported sector size %u\n",
                      dev->name, bpb->bpb_bytes_per_sector);
        return NULL;
    }
    if (bpb->bpb_root_entries != 0 || bpb->bpb_fat_size_16 != 0) {
        serial_printf("[FAT32] %s: not FAT32 (FAT12/16 detected)\n", dev->name);
        return NULL;
    }
    if (bpb->bpb_fat_size_32 == 0) {
        serial_printf("[FAT32] %s: FAT size 0\n", dev->name);
        return NULL;
    }

    fat32_t *fs = calloc(1, sizeof(fat32_t));
    if (!fs) return NULL;

    fs->dev                 = dev;
    fs->bytes_per_sector    = bpb->bpb_bytes_per_sector;
    fs->sectors_per_cluster = bpb->bpb_sectors_per_cluster;
    fs->bytes_per_cluster   = fs->bytes_per_sector * fs->sectors_per_cluster;
    fs->reserved_sectors    = bpb->bpb_reserved_sectors;
    fs->num_fats            = bpb->bpb_num_fats;
    fs->fat_size_sectors    = bpb->bpb_fat_size_32;
    fs->total_sectors       = bpb->bpb_total_sectors_32
                                ? bpb->bpb_total_sectors_32
                                : bpb->bpb_total_sectors_16;
    fs->root_cluster        = bpb->bpb_root_cluster;
    fs->first_fat_sector    = fs->reserved_sectors;
    fs->first_data_sector   = fs->reserved_sectors +
                              (fs->num_fats * fs->fat_size_sectors);
    uint32_t data_sectors   = fs->total_sectors - fs->first_data_sector;
    fs->total_clusters      = data_sectors / fs->sectors_per_cluster;
    fs->fsinfo_sector       = bpb->bpb_fs_info;
    fs->next_free_hint      = 2;
    fs->free_count          = 0xFFFFFFFF;
    fs->readonly            = false;

    fs->shared_buf = (uint8_t *)malloc(fs->bytes_per_cluster);
    if (!fs->shared_buf) { free(fs); return NULL; }
    if (fs->bytes_per_cluster > MAX_CLUSTER_BYTES) {
        serial_printf("[FAT32] %s: cluster too large %u, bailing\n",
                      dev->name, fs->bytes_per_cluster);
        free(fs);
        return NULL;
    }

    if (fs->fsinfo_sector) {
        uint8_t fsibuf[FAT32_SECTOR_SIZE];
        if (read_sector(fs, fs->fsinfo_sector, fsibuf) == 0) {
            fat32_fsinfo_t *fsi = (fat32_fsinfo_t *)fsibuf;
            if (fsi->lead_sig == FAT32_FSINFO_LEAD_SIG
                && fsi->struct_sig == FAT32_FSINFO_STRUCT_SIG) {
                fs->free_count     = fsi->free_count;
                if (fsi->next_free >= 2 && fsi->next_free < fs->total_clusters + 2)
                    fs->next_free_hint = fsi->next_free;
            }
        }
    }

    serial_printf("[FAT32] mounted %s: clusters=%u cluster_size=%u root_cluster=%u free=%u\n",
                  dev->name, fs->total_clusters, fs->bytes_per_cluster,
                  fs->root_cluster, fs->free_count);

    vnode_t *root = fat32_alloc_vnode(fs, fs->root_cluster, 0,
                                      FAT_ATTR_DIRECTORY, 0, 0);
    return root;
}

void fat32_unmount(fat32_t *fs) {
    if (!fs) return;
    fat32_sync(fs);
    if (fs->shared_buf) { free(fs->shared_buf); fs->shared_buf = NULL; }
    free(fs);
}

int fat32_format(blkdev_t *dev, const char *label) {
    if (!dev) return -EINVAL;

    uint32_t total_sectors = (uint32_t)dev->sector_count;
    if (total_sectors < 65536) {
        serial_printf("[FAT32] device too small (%u sectors) for FAT32\n", total_sectors);
        return -EINVAL;
    }

    uint32_t sectors_per_cluster;
    if      (total_sectors <=    532480) sectors_per_cluster = 1;
    else if (total_sectors <=  16777216) sectors_per_cluster = 8;
    else if (total_sectors <=  33554432) sectors_per_cluster = 16;
    else if (total_sectors <=  67108864) sectors_per_cluster = 32;
    else                                 sectors_per_cluster = 64;

    uint32_t reserved = 32;
    uint32_t num_fats = 2;

    uint32_t data_sectors = total_sectors - reserved;
    uint32_t fat_size;
    uint32_t cluster_count;
    for (fat_size = 1; ; fat_size++) {
        uint32_t total_fat_sectors = fat_size * num_fats;
        if (total_fat_sectors >= data_sectors) return -EINVAL;
        uint32_t remaining = data_sectors - total_fat_sectors;
        cluster_count = remaining / sectors_per_cluster;
        uint32_t need_entries = cluster_count + 2;
        uint32_t need_bytes   = need_entries * 4;
        uint32_t need_sectors = (need_bytes + 511) / 512;
        if (need_sectors <= fat_size) break;
        if (fat_size > 1024 * 1024) return -EINVAL;
    }

    if (cluster_count < 65525) {
        serial_printf("[FAT32] ERROR: only %u clusters on %u-sector volume.\n"
                      "        FAT32 requires >= 65525 clusters.\n"
                      "        Minimum volume size is ~33 MB with 512-byte clusters.\n"
                      "        Use a larger partition.\n",
                      cluster_count, total_sectors);
        return -EINVAL;
    }

    uint8_t sec[FAT32_SECTOR_SIZE];

    memset(sec, 0, sizeof(sec));
    fat32_bpb_t *bpb = (fat32_bpb_t *)sec;
    bpb->bs_jmp[0] = 0xEB; bpb->bs_jmp[1] = 0x58; bpb->bs_jmp[2] = 0x90;
    memcpy(bpb->bs_oem, "CERVUS  ", 8);
    bpb->bpb_bytes_per_sector    = 512;
    bpb->bpb_sectors_per_cluster = (uint8_t)sectors_per_cluster;
    bpb->bpb_reserved_sectors    = (uint16_t)reserved;
    bpb->bpb_num_fats            = (uint8_t)num_fats;
    bpb->bpb_root_entries        = 0;
    bpb->bpb_total_sectors_16    = 0;
    bpb->bpb_media               = 0xF8;
    bpb->bpb_fat_size_16         = 0;
    bpb->bpb_sectors_per_track   = 63;
    bpb->bpb_num_heads           = 255;
    bpb->bpb_hidden_sectors      = 0;
    bpb->bpb_total_sectors_32    = total_sectors;
    bpb->bpb_fat_size_32         = fat_size;
    bpb->bpb_ext_flags           = 0;
    bpb->bpb_fs_version          = 0;
    bpb->bpb_root_cluster        = 2;
    bpb->bpb_fs_info             = 1;
    bpb->bpb_backup_boot         = 6;
    bpb->bs_drive_num            = 0x80;
    bpb->bs_boot_sig             = 0x29;
    bpb->bs_vol_id               = 0xCE7005;
    memset(bpb->bs_vol_label, ' ', 11);
    if (label) {
        for (int i = 0; i < 11 && label[i]; i++) {
            char c = label[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            bpb->bs_vol_label[i] = (uint8_t)c;
        }
    }
    memcpy(bpb->bs_fs_type, "FAT32   ", 8);
    bpb->bs_signature            = 0xAA55;
    if (dev->ops->write_sectors(dev, 0, 1, sec) < 0) return -EIO;
    if (dev->ops->write_sectors(dev, 6, 1, sec) < 0) return -EIO;

    memset(sec, 0, sizeof(sec));
    fat32_fsinfo_t *fsi = (fat32_fsinfo_t *)sec;
    fsi->lead_sig   = FAT32_FSINFO_LEAD_SIG;
    fsi->struct_sig = FAT32_FSINFO_STRUCT_SIG;
    fsi->free_count = cluster_count - 1;
    fsi->next_free  = 3;
    fsi->trail_sig  = FAT32_FSINFO_TRAIL_SIG;
    if (dev->ops->write_sectors(dev, 1, 1, sec) < 0) return -EIO;
    if (dev->ops->write_sectors(dev, 7, 1, sec) < 0) return -EIO;

    {
        static uint8_t zeros_batch[128 * FAT32_SECTOR_SIZE];
        memset(zeros_batch, 0, sizeof(zeros_batch));
        for (uint32_t f = 0; f < num_fats; f++) {
            uint32_t fat_start = reserved + f * fat_size;
            uint32_t remaining = fat_size;
            uint32_t sector = fat_start;
            serial_printf("[fat32] zeroing FAT#%u: %u sectors starting at LBA %u\n",
                          f, fat_size, fat_start);
            printf("       FAT#%u: ", f);
            uint32_t last_pct = 0;
            uint32_t spinner = 0;
            while (remaining > 0) {
                uint32_t batch = (remaining > 128) ? 128 : remaining;
                int r = dev->ops->write_sectors(dev, sector, batch, zeros_batch);
                if (r < 0) {
                    serial_printf("[fat32] FAT zero-fill FAILED at LBA %u (batch=%u): %d\n",
                                  sector, batch, r);
                    printf("\n");
                    return -EIO;
                }
                sector += batch;
                remaining -= batch;
                uint32_t done = fat_size - remaining;
                uint32_t pct = (done * 100) / fat_size;
                if (pct != last_pct) {
                    const char glyphs[4] = { '|', '/', '-', '\\' };
                    printf("\r\033[K       %c FAT#%u: %u%%",
                           glyphs[spinner & 3], f, pct);
                    spinner++;
                    last_pct = pct;
                }
            }
            printf("\r\033[K       FAT#%u: done\n", f);
            if (dev->ops->flush) dev->ops->flush(dev);
        }
    }
    uint32_t fat0[3] = { 0x0FFFFF00 | 0xF8, 0x0FFFFFFF, FAT32_EOC };
    for (uint32_t f = 0; f < num_fats; f++) {
        uint8_t fat_first[FAT32_SECTOR_SIZE];
        memset(fat_first, 0, FAT32_SECTOR_SIZE);
        memcpy(fat_first, fat0, sizeof(fat0));
        if (dev->ops->write_sectors(dev, reserved + f * fat_size, 1, fat_first) < 0)
            return -EIO;
    }

    uint32_t first_data_sector = reserved + num_fats * fat_size;
    uint32_t root_lba = first_data_sector;
    uint8_t zero[FAT32_SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));
    for (uint32_t i = 0; i < sectors_per_cluster; i++) {
        if (dev->ops->write_sectors(dev, root_lba + i, 1, zero) < 0) return -EIO;
    }

    if (dev->ops->flush) dev->ops->flush(dev);
    serial_printf("[FAT32] formatted %s: %u clusters, cluster_size=%u, fat_size=%u sectors\n",
                  dev->name, cluster_count, sectors_per_cluster * 512, fat_size);
    return 0;
}

int fat32_statvfs(vnode_t *root, vfs_statvfs_t *out) {
    if (!root || !root->fs_data || !out) return -EINVAL;
    fat32_vdata_t *vd = (fat32_vdata_t *)root->fs_data;
    fat32_t *fs = vd->fs;
    if (!fs) return -EINVAL;

    out->f_bsize   = fs->bytes_per_cluster;
    out->f_blocks  = fs->total_clusters;

    if (fs->free_count == 0xFFFFFFFF) {
        uint32_t free = 0;
        for (uint32_t cl = 2; cl < fs->total_clusters + 2; cl++) {
            uint32_t entry = fat_read_entry(fs, cl) & 0x0FFFFFFF;
            if (entry == 0) free++;
        }
        fs->free_count = free;
    }

    out->f_bfree   = fs->free_count;
    out->f_bavail  = fs->free_count;
    out->f_files   = 0;
    out->f_ffree   = 0;
    out->f_flag    = 0;
    out->f_namemax = 255;
    return 0;
}