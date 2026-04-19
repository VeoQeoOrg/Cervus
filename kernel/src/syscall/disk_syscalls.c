#include "../../include/syscall/syscall.h"
#include "../../include/syscall/syscall_nums.h"
#include "../../include/syscall/errno.h"
#include "../../include/drivers/disk.h"
#include "../../include/drivers/blkdev.h"
#include "../../include/drivers/ata.h"
#include "../../include/drivers/partition.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/capabilities.h"
#include "../../include/smp/percpu.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/fs/vfs.h"
#include "../../include/fs/ext2.h"
#include "../../include/fs/fat32.h"
#include <string.h>
#include <stdlib.h>

static inline task_t* disk_cur_task(void) {
    percpu_t* pc = get_percpu();
    return pc ? (task_t*)pc->current_task : NULL;
}

static int disk_strncpy_from_user(char *dst, const char *src, size_t max) {
    uintptr_t addr = (uintptr_t)src;
    if (addr < 0x1000ULL || addr >= 0x0000800000000000ULL) return -EFAULT;
    for (size_t i = 0; i < max - 1; i++) {
        dst[i] = src[i];
        if (!dst[i]) return (int)i;
    }
    dst[max-1] = '\0';
    return (int)(max-1);
}

static int user_ptr_ok(uint64_t p) {
    return p >= 0x1000ULL && p < 0x0000800000000000ULL;
}

int64_t sys_disk_mount(uint64_t devname_ptr, uint64_t path_ptr, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64], path[256];
    if (disk_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (disk_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0) return -EFAULT;
    serial_printf("[SYSCALL] disk_mount('%s', '%s') by pid=%u\n", devname, path, t->pid);
    return disk_mount(devname, path);
}

int64_t sys_disk_umount(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;
    char path[256];
    if (disk_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0) return -EFAULT;
    return disk_umount(path);
}

int64_t sys_disk_format(uint64_t devname_ptr, uint64_t label_ptr,
                        uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64], label[64];
    if (disk_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (label_ptr) {
        if (disk_strncpy_from_user(label, (const char *)label_ptr, sizeof(label)) < 0) return -EFAULT;
    } else {
        strncpy(label, devname, sizeof(label) - 1);
    }
    return disk_format(devname, label);
}

int64_t sys_disk_info(uint64_t index, uint64_t buf_ptr, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!buf_ptr) return -EINVAL;
    blkdev_t *dev = blkdev_get((int)index);
    if (!dev || !dev->present) return -ENODEV;
    struct {
        char     name[32];
        uint64_t sectors;
        uint64_t size_bytes;
        char     model[41];
        uint8_t  present;
        uint8_t  _pad[6];
    } info;
    memset(&info, 0, sizeof(info));
    strncpy(info.name, dev->name, 31);
    info.sectors    = dev->sector_count;
    info.size_bytes = dev->size_bytes;
    info.present    = 1;
    ata_drive_t *ata = (ata_drive_t *)dev->priv;
    if (ata) strncpy(info.model, ata->model, 40);
    if (!user_ptr_ok(buf_ptr)) return -EFAULT;
    memcpy((void *)buf_ptr, &info, sizeof(info));
    return 0;
}

int64_t sys_disk_read_raw(uint64_t devname_ptr, uint64_t lba, uint64_t count,
                          uint64_t buf_ptr, uint64_t a5, uint64_t a6)
{
    (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64];
    if (disk_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (!user_ptr_ok(buf_ptr)) return -EFAULT;
    if (count == 0 || count > 256) return -EINVAL;

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;
    if (lba + count > dev->sector_count) return -EINVAL;

    int r = dev->ops->read_sectors(dev, lba, (uint32_t)count, (void *)buf_ptr);
    if (r < 0) return r;
    return (int64_t)(count * dev->sector_size);
}

int64_t sys_disk_write_raw(uint64_t devname_ptr, uint64_t lba, uint64_t count,
                           uint64_t buf_ptr, uint64_t a5, uint64_t a6)
{
    (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64];
    if (disk_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (!user_ptr_ok(buf_ptr)) return -EFAULT;
    if (count == 0 || count > 256) return -EINVAL;

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;
    if (lba + count > dev->sector_count) return -EINVAL;

    int r = dev->ops->write_sectors(dev, lba, (uint32_t)count, (const void *)buf_ptr);
    if (r < 0) return r;
    if (dev->ops->flush) dev->ops->flush(dev);
    return (int64_t)(count * dev->sector_size);
}

int64_t sys_disk_partition(uint64_t devname_ptr, uint64_t specs_ptr, uint64_t nparts,
                           uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64];
    if (disk_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (!user_ptr_ok(specs_ptr)) return -EFAULT;
    if (nparts == 0 || nparts > 4) return -EINVAL;

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;

    cervus_mbr_part_t specs[4];
    memset(specs, 0, sizeof(specs));
    memcpy(specs, (const void *)specs_ptr, sizeof(cervus_mbr_part_t) * nparts);

    mbr_partition_t parts[4];
    memset(parts, 0, sizeof(parts));
    for (uint64_t i = 0; i < nparts; i++) {
        parts[i].boot_flag    = specs[i].boot_flag ? 0x80 : 0x00;
        parts[i].type         = specs[i].type;
        parts[i].lba_start    = specs[i].lba_start;
        parts[i].sector_count = specs[i].sector_count;
        parts[i].chs_start[0] = 0xFE;
        parts[i].chs_start[1] = 0xFF;
        parts[i].chs_start[2] = 0xFF;
        parts[i].chs_end[0]   = 0xFE;
        parts[i].chs_end[1]   = 0xFF;
        parts[i].chs_end[2]   = 0xFF;
    }

    uint32_t sig = 0xCE705CE7;
    int r = partition_write_mbr(dev, parts, sig);
    if (r < 0) return r;
    if (dev->ops->flush) dev->ops->flush(dev);

    partition_scan(dev);
    return 0;
}

int64_t sys_disk_mkfs_fat32(uint64_t devname_ptr, uint64_t label_ptr,
                            uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1))) return -EPERM;

    char devname[64], label[16];
    if (disk_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0) return -EFAULT;
    if (label_ptr) {
        if (disk_strncpy_from_user(label, (const char *)label_ptr, sizeof(label)) < 0) return -EFAULT;
    } else {
        strncpy(label, "CERVUS", sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
    }

    const char *name = devname;
    if (strncmp(name, "/dev/", 5) == 0) name += 5;
    blkdev_t *dev = blkdev_get_by_name(name);
    if (!dev) return -ENODEV;

    return fat32_format(dev, label);
}

int64_t sys_disk_bios_install(uint64_t a1, uint64_t a2, uint64_t a3,
                              uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    const char *disk_name = (const char *)a1;
    const void *sys_data  = (const void *)a2;
    uint32_t    sys_size  = (uint32_t)a3;

    if (!disk_name || !sys_data || sys_size < 512) return -EINVAL;

    blkdev_t *dev = blkdev_get_by_name(disk_name);
    if (!dev) return -ENOENT;
    if (dev->sector_size != 512) return -EINVAL;

    uint8_t sector0[512];
    int r = dev->ops->read_sectors(dev, 0, 1, sector0);
    if (r < 0) return r;

    uint8_t saved_timestamp[6];
    uint8_t saved_parttable[70];
    memcpy(saved_timestamp, sector0 + 218, 6);
    memcpy(saved_parttable, sector0 + 440, 70);

    const uint8_t *src = (const uint8_t *)sys_data;

    memcpy(sector0, src, 512);

    memcpy(sector0 + 218, saved_timestamp, 6);
    memcpy(sector0 + 440, saved_parttable, 70);

    uint64_t stage2_loc = 512;
    memcpy(sector0 + 0x1A4, &stage2_loc, 8);

    uint32_t stage2_bytes   = sys_size - 512;
    uint32_t stage2_sectors = (stage2_bytes + 511) / 512;

    if (1 + stage2_sectors >= 2048) return -ENOSPC;

    uint8_t sector_buf[512];
    for (uint32_t i = 0; i < stage2_sectors; i++) {
        uint32_t off = i * 512;
        uint32_t take = (stage2_bytes - off >= 512) ? 512 : (stage2_bytes - off);
        memset(sector_buf, 0, 512);
        memcpy(sector_buf, src + 512 + off, take);
        r = dev->ops->write_sectors(dev, 1 + i, 1, sector_buf);
        if (r < 0) return r;
    }

    r = dev->ops->write_sectors(dev, 0, 1, sector0);
    if (r < 0) return r;

    if (dev->ops->flush) dev->ops->flush(dev);

    serial_printf("[bios-install] deployed: stage1=512B at LBA 0, stage2=%uB at LBA 1..%u\n",
                  stage2_bytes, stage2_sectors);
    return 0;
}

int64_t sys_disk_list_parts(uint64_t out_ptr, uint64_t max,
                            uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_ptr_ok(out_ptr)) return -EFAULT;
    if (max == 0) return -EINVAL;

    cervus_part_info_t *out = (cervus_part_info_t *)out_ptr;
    int total = blkdev_count();
    uint64_t written = 0;

    for (int i = 0; i < total && written < max; i++) {
        blkdev_t *d = blkdev_get(i);
        if (!d || !d->present) continue;

        size_t nlen = strlen(d->name);
        bool is_part = false;
        if (nlen >= 2) {
            for (size_t k = 0; k < nlen; k++) {
                if (d->name[k] >= '0' && d->name[k] <= '9') { is_part = true; break; }
            }
        }
        cervus_part_info_t info;
        memset(&info, 0, sizeof(info));
        strncpy(info.part_name, d->name, sizeof(info.part_name) - 1);
        if (is_part) {
            char base[32]; int bi = 0;
            for (size_t k = 0; k < nlen && bi < 31; k++) {
                if (d->name[k] >= '0' && d->name[k] <= '9') break;
                base[bi++] = d->name[k];
            }
            base[bi] = '\0';
            strncpy(info.disk_name, base, sizeof(info.disk_name) - 1);
            info.part_num = (uint32_t)atoi(d->name + bi);
        } else {
            strncpy(info.disk_name, d->name, sizeof(info.disk_name) - 1);
            info.part_num = 0;
        }
        info.size_bytes   = d->size_bytes;
        info.sector_count = d->sector_count;
        info.lba_start    = 0;
        info.type         = 0;
        info.bootable     = 0;
        memcpy(&out[written], &info, sizeof(info));
        written++;
    }
    return (int64_t)written;
}

int64_t sys_unlink(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
    char path[256];
    if (disk_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0) return -EFAULT;
    char dirpath[256]; strncpy(dirpath, path, 255);
    char *slash = NULL;
    for (int i = (int)strlen(dirpath)-1; i >= 0; i--) { if (dirpath[i]=='/') { slash=&dirpath[i]; break; } }
    if (!slash) return -EINVAL;
    char name[256]; strncpy(name, slash+1, 255);
    if (slash==dirpath) dirpath[1]='\0'; else *slash='\0';
    vnode_t *dir = NULL;
    int r = vfs_lookup(dirpath, &dir);
    if (r<0) return r;
    if (!dir->ops || !dir->ops->unlink) { vnode_unref(dir); return -ENOSYS; }
    r = dir->ops->unlink(dir, name);
    vnode_unref(dir);
    if (r == 0) vfs_sync_all();
    return r;
}

int64_t sys_rmdir(uint64_t p, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    return sys_unlink(p,a2,a3,a4,a5,a6);
}

int64_t sys_mkdir(uint64_t path_ptr, uint64_t mode, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6;
    char path[256];
    if (disk_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0) return -EFAULT;
    int r = vfs_mkdir(path, (uint32_t)mode);
    if (r == 0) vfs_sync_all();
    return r;
}

int64_t sys_rename(uint64_t old_ptr, uint64_t new_ptr, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6;
    char oldp[256], newp[256];
    if (disk_strncpy_from_user(oldp, (const char *)old_ptr, 256) < 0) return -EFAULT;
    if (disk_strncpy_from_user(newp, (const char *)new_ptr, 256) < 0) return -EFAULT;

    vnode_t *src_node = NULL;
    int r = vfs_lookup(oldp, &src_node);
    if (r < 0) return r;

    vnode_t *dst_node = NULL;
    if (vfs_lookup(newp, &dst_node) == 0) {
        if (dst_node->type == VFS_NODE_DIR) {
            const char *base = oldp;
            for (const char *p = oldp; *p; p++) if (*p == '/') base = p + 1;
            size_t dlen = strlen(newp);
            if (dlen + 1 + strlen(base) < 255) {
                if (newp[dlen-1] != '/') { newp[dlen] = '/'; newp[dlen+1] = '\0'; dlen++; }
                strncat(newp, base, 254 - dlen);
            }
        }
        vnode_unref(dst_node);
    }

    vfs_file_t *src_f = NULL, *dst_f = NULL;
    r = vfs_open(oldp, O_RDONLY, 0, &src_f);
    if (r < 0) { vnode_unref(src_node); return r; }
    r = vfs_open(newp, O_WRONLY | O_CREAT | O_TRUNC, src_node->mode, &dst_f);
    if (r < 0) { vfs_close(src_f); vnode_unref(src_node); return r; }
    char buf[512]; int64_t n;
    while ((n = vfs_read(src_f, buf, sizeof(buf))) > 0) vfs_write(dst_f, buf, (size_t)n);
    vfs_close(src_f);
    vfs_close(dst_f);
    vnode_unref(src_node);

    char dirp[256]; strncpy(dirp, oldp, 255); dirp[255] = '\0';
    char *sl = NULL;
    for (int i = (int)strlen(dirp)-1; i >= 0; i--) if (dirp[i] == '/') { sl = &dirp[i]; break; }
    if (sl) {
        char nm[256]; strncpy(nm, sl+1, 255);
        if (sl == dirp) dirp[1] = '\0'; else *sl = '\0';
        vnode_t *dir = NULL;
        if (vfs_lookup(dirp, &dir) == 0) {
            if (dir->ops && dir->ops->unlink) dir->ops->unlink(dir, nm);
            vnode_unref(dir);
        }
    }
    vfs_sync_all();
    return 0;
}

int64_t sys_list_mounts(uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    vfs_mount_info_t *out = (vfs_mount_info_t *)a1;
    int max = (int)a2;
    if (!out || max <= 0) return -EINVAL;
    return vfs_list_mounts(out, max);
}

int64_t sys_statvfs(uint64_t a1, uint64_t a2, uint64_t a3,
                    uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    const char *path = (const char *)a1;
    vfs_statvfs_t *out = (vfs_statvfs_t *)a2;
    if (!path || !out) return -EINVAL;
    return vfs_statvfs(path, out);
}