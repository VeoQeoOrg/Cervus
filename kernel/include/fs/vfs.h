#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../syscall/errno.h"

#define VFS_MAX_PATH        512
#define VFS_MAX_NAME        256
#define VFS_MAX_MOUNTS       16
#define VFS_MAX_OPEN_FILES  512

#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002
#define O_ACCMODE   0x003
#define O_CREAT     0x040
#define O_TRUNC     0x200
#define O_APPEND    0x400
#define O_DIRECTORY 0x10000
#define O_NONBLOCK  0x800

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define FD_CLOEXEC  (1 << 0)
#define TASK_MAX_FDS 256

typedef enum {
    VFS_NODE_FILE    = 0,
    VFS_NODE_DIR     = 1,
    VFS_NODE_CHARDEV = 2,
    VFS_NODE_BLKDEV  = 3,
    VFS_NODE_SYMLINK = 4,
    VFS_NODE_PIPE    = 5,
} vnode_type_t;

typedef struct {
    uint64_t        st_ino;
    vnode_type_t    st_type;
    uint32_t        st_mode;
    uint32_t        st_uid;
    uint32_t        st_gid;
    uint64_t        st_size;
    uint64_t        st_blocks;
} vfs_stat_t;

typedef struct {
    uint64_t    d_ino;
    uint8_t     d_type;
    char        d_name[VFS_MAX_NAME];
} vfs_dirent_t;

typedef struct vnode     vnode_t;
typedef struct vfs_mount vfs_mount_t;

typedef struct vnode_ops {
    int64_t (*read)    (vnode_t *node, void *buf, size_t len, uint64_t offset);
    int64_t (*write)   (vnode_t *node, const void *buf, size_t len, uint64_t offset);
    int     (*truncate)(vnode_t *node, uint64_t new_size);
    int     (*lookup)  (vnode_t *dir, const char *name, vnode_t **out);
    int     (*readdir) (vnode_t *dir, uint64_t index, vfs_dirent_t *out);
    int     (*mkdir)   (vnode_t *dir, const char *name, uint32_t mode);
    int     (*create)  (vnode_t *dir, const char *name, uint32_t mode, vnode_t **out);
    int     (*unlink)  (vnode_t *dir, const char *name);
    int     (*rename)  (vnode_t *src_dir, const char *src_name,
                        vnode_t *dst_dir, const char *dst_name);
    int     (*stat)    (vnode_t *node, vfs_stat_t *out);
    void    (*ref)     (vnode_t *node);
    void    (*unref)   (vnode_t *node);
    int64_t (*ioctl)   (vnode_t *node, uint64_t req, void *arg);
} vnode_ops_t;

struct vnode {
    vnode_type_t        type;
    uint32_t            mode;
    uint32_t            uid;
    uint32_t            gid;
    uint64_t            size;
    uint64_t            ino;
    const vnode_ops_t  *ops;
    void               *fs_data;
    volatile int        refcount;
    vfs_mount_t        *mounted;
};

struct vfs_mount {
    char        path[VFS_MAX_PATH];
    char        device[32];
    char        fstype[16];
    vnode_t    *root;
    bool        used;
    void       *fs_priv;
    void      (*unmount)(void *fs_priv);
    void      (*sync)(void *fs_priv);
};

typedef struct {
    vnode_t         *vnode;
    uint64_t         offset;
    int              flags;
    volatile int     refcount;
} vfs_file_t;

typedef struct {
    vfs_file_t  *file;
    int          fd_flags;
} fd_entry_t;

typedef struct fd_table fd_table_t;

struct fd_table {
    fd_entry_t entries[TASK_MAX_FDS];
};

void    vfs_init   (void);
int     vfs_mount  (const char *path, vnode_t *fs_root);

int     vfs_mount_fs(const char *path, vnode_t *fs_root,
                     void *fs_priv, void (*unmount_fn)(void *),
                     void (*sync_fn)(void *));

int     vfs_umount (const char *path);
int     vfs_lookup (const char *path, vnode_t **out);

int     vfs_open   (const char *path, int flags, uint32_t mode, vfs_file_t **out);
void    vfs_close  (vfs_file_t *file);
int64_t vfs_read   (vfs_file_t *file, void *buf, size_t len);
int64_t vfs_write  (vfs_file_t *file, const void *buf, size_t len);
int64_t vfs_seek   (vfs_file_t *file, int64_t offset, int whence);
int     vfs_stat   (const char *path, vfs_stat_t *out);
int     vfs_fstat  (vfs_file_t *file, vfs_stat_t *out);
int64_t vfs_ioctl  (vfs_file_t *file, uint64_t req, void *arg);
int     vfs_readdir(vfs_file_t *file, vfs_dirent_t *out);
int     vfs_mkdir  (const char *path, uint32_t mode);

void        vnode_ref    (vnode_t *node);
void        vnode_unref  (vnode_t *node);
vfs_file_t *vfs_file_alloc(void);
void        vfs_file_free (vfs_file_t *file);

fd_table_t *fd_table_create (void);
fd_table_t *fd_table_clone  (const fd_table_t *src);
void        fd_table_cloexec(fd_table_t *table);
void        fd_table_destroy(fd_table_t *table);

int         fd_alloc    (fd_table_t *table, vfs_file_t *file, int min_fd);
vfs_file_t *fd_get      (const fd_table_t *table, int fd);
int         fd_close    (fd_table_t *table, int fd);
int         fd_dup2     (fd_table_t *table, int oldfd, int newfd);
int         fd_set_flags(fd_table_t *table, int fd, int flags);
int         fd_get_flags(const fd_table_t *table, int fd);
void vfs_sync_all(void);
int vfs_init_stdio(void *task_ptr);

typedef struct {
    char path[VFS_MAX_PATH];
    char device[32];
    char fstype[16];
    uint32_t flags;
} vfs_mount_info_t;

int vfs_set_mount_info(const char *path, const char *device, const char *fstype);
int vfs_list_mounts(vfs_mount_info_t *out, int max);

typedef struct {
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_flag;
    uint32_t f_namemax;
} vfs_statvfs_t;

int vfs_statvfs(const char *path, vfs_statvfs_t *out);

#endif