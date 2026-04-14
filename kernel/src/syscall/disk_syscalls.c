#include "../../include/syscall/syscall.h"
#include "../../include/syscall/syscall_nums.h"
#include "../../include/syscall/errno.h"
#include "../../include/drivers/disk.h"
#include "../../include/drivers/blkdev.h"
#include "../../include/drivers/ata.h"
#include "../../include/sched/sched.h"
#include "../../include/sched/capabilities.h"
#include "../../include/smp/percpu.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/fs/vfs.h"
#include "../../include/fs/ext2.h"
#include <string.h>

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

int64_t sys_disk_mount(uint64_t devname_ptr, uint64_t path_ptr, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;

    if (t->uid != 0 && !(t->capabilities & (1ULL << 1)))
        return -EPERM;

    char devname[64], path[256];
    if (disk_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0)
        return -EFAULT;
    if (disk_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0)
        return -EFAULT;

    serial_printf("[SYSCALL] disk_mount('%s', '%s') by pid=%u\n",
                  devname, path, t->pid);

    return disk_mount(devname, path);
}

int64_t sys_disk_umount(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1)))
        return -EPERM;

    char path[256];
    if (disk_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0)
        return -EFAULT;

    return disk_umount(path);
}

int64_t sys_disk_format(uint64_t devname_ptr, uint64_t label_ptr,
                        uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    task_t *t = disk_cur_task();
    if (!t) return -ESRCH;
    if (t->uid != 0 && !(t->capabilities & (1ULL << 1)))
        return -EPERM;

    char devname[64], label[64];
    if (disk_strncpy_from_user(devname, (const char *)devname_ptr, sizeof(devname)) < 0)
        return -EFAULT;
    if (label_ptr) {
        if (disk_strncpy_from_user(label, (const char *)label_ptr, sizeof(label)) < 0)
            return -EFAULT;
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

    uintptr_t addr = (uintptr_t)buf_ptr;
    if (addr < 0x1000ULL || addr >= 0x0000800000000000ULL) return -EFAULT;

    memcpy((void *)buf_ptr, &info, sizeof(info));
    return 0;
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
            for (const char *p = oldp; *p; p++)
                if (*p == '/') base = p + 1;
            size_t dlen = strlen(newp);
            if (dlen + 1 + strlen(base) < 255) {
                if (newp[dlen-1] != '/') { newp[dlen] = '/'; newp[dlen+1] = '\0'; dlen++; }
                strncat(newp, base, 254 - dlen);
            }
        }
        vnode_unref(dst_node);
    }

    if (src_node->type == VFS_NODE_DIR) {
        vnode_unref(src_node);

        char src_dirp[256]; strncpy(src_dirp, oldp, 255); src_dirp[255] = '\0';
        char src_name[256];
        char *src_sl = NULL;
        for (int i = (int)strlen(src_dirp)-1; i >= 0; i--)
            if (src_dirp[i] == '/') { src_sl = &src_dirp[i]; break; }
        if (!src_sl) return -EINVAL;
        strncpy(src_name, src_sl+1, 255);
        if (src_sl == src_dirp) src_dirp[1] = '\0'; else *src_sl = '\0';

        char dst_dirp[256]; strncpy(dst_dirp, newp, 255); dst_dirp[255] = '\0';
        char dst_name[256];
        char *dst_sl = NULL;
        for (int i = (int)strlen(dst_dirp)-1; i >= 0; i--)
            if (dst_dirp[i] == '/') { dst_sl = &dst_dirp[i]; break; }
        if (!dst_sl) return -EINVAL;
        strncpy(dst_name, dst_sl+1, 255);
        if (dst_sl == dst_dirp) dst_dirp[1] = '\0'; else *dst_sl = '\0';

        vnode_t *src_dir = NULL, *dst_dir = NULL;
        r = vfs_lookup(src_dirp, &src_dir);
        if (r < 0) return r;
        r = vfs_lookup(dst_dirp, &dst_dir);
        if (r < 0) { vnode_unref(src_dir); return r; }

        if (!src_dir->ops || !src_dir->ops->rename) {
            vnode_unref(src_dir); vnode_unref(dst_dir);
            return -ENOSYS;
        }
        r = src_dir->ops->rename(src_dir, src_name, dst_dir, dst_name);
        vnode_unref(src_dir);
        vnode_unref(dst_dir);
        if (r == 0) vfs_sync_all();
        return r;
    }

    vfs_file_t *src_f = NULL, *dst_f = NULL;
    r = vfs_open(oldp, O_RDONLY, 0, &src_f);
    if (r < 0) { vnode_unref(src_node); return r; }
    r = vfs_open(newp, O_WRONLY | O_CREAT | O_TRUNC, src_node->mode, &dst_f);
    if (r < 0) { vfs_close(src_f); vnode_unref(src_node); return r; }
    char buf[512]; int64_t n;
    while ((n = vfs_read(src_f, buf, sizeof(buf))) > 0)
        vfs_write(dst_f, buf, (size_t)n);
    vfs_close(src_f);
    vfs_close(dst_f);
    vnode_unref(src_node);

    char dirp[256]; strncpy(dirp, oldp, 255); dirp[255] = '\0';
    char *sl = NULL;
    for (int i = (int)strlen(dirp)-1; i >= 0; i--)
        if (dirp[i] == '/') { sl = &dirp[i]; break; }
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