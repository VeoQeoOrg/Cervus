#include "../../include/fs/vfs.h"
#include "../../include/sched/sched.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <string.h>

static vfs_mount_t  g_mounts[VFS_MAX_MOUNTS];
static vfs_file_t   g_open_files[VFS_MAX_OPEN_FILES];
static bool         g_vfs_ready = false;

void vfs_init(void) {
    memset(g_mounts,     0, sizeof(g_mounts));
    memset(g_open_files, 0, sizeof(g_open_files));
    g_vfs_ready = true;
    serial_writestring("[VFS] initialized\n");
}

void vnode_ref(vnode_t *node) {
    if (!node) return;
    if (node->ops && node->ops->ref)
        node->ops->ref(node);
    else
        __atomic_fetch_add(&node->refcount, 1, __ATOMIC_RELAXED);
}

void vnode_unref(vnode_t *node) {
    if (!node) return;
    if (node->ops && node->ops->unref)
        node->ops->unref(node);
    else
        __atomic_fetch_sub(&node->refcount, 1, __ATOMIC_RELAXED);
}

int vfs_mount(const char *path, vnode_t *fs_root) {
    if (!path || !fs_root) return -EINVAL;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && strcmp(g_mounts[i].path, path) == 0) {
            serial_printf("[VFS] mount: '%s' already mounted\n", path);
            return -EBUSY;
        }
    }

    int slot = -1;
    if (strcmp(path, "/") == 0) {
        slot = 0;
    } else {
        for (int i = 1; i < VFS_MAX_MOUNTS; i++) {
            if (!g_mounts[i].used) { slot = i; break; }
        }
    }
    if (slot < 0) return -ENOMEM;

    strncpy(g_mounts[slot].path, path, VFS_MAX_PATH - 1);
    g_mounts[slot].root = fs_root;
    g_mounts[slot].used = true;
    vnode_ref(fs_root);
    serial_printf("[VFS] mounted '%s' at slot %d\n", path, slot);
    return 0;
}

int vfs_umount(const char *path) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && strcmp(g_mounts[i].path, path) == 0) {
            vnode_unref(g_mounts[i].root);
            memset(&g_mounts[i], 0, sizeof(vfs_mount_t));
            return 0;
        }
    }
    return -ENOENT;
}

static vfs_mount_t *find_mount(const char *path, const char **rel_out) {
    vfs_mount_t *best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used) continue;
        size_t mlen = strlen(g_mounts[i].path);
        if (strncmp(path, g_mounts[i].path, mlen) == 0) {
            if (path[mlen] == '/' || path[mlen] == '\0' || mlen == 1) {
                if (mlen > best_len) {
                    best     = &g_mounts[i];
                    best_len = mlen;
                }
            }
        }
    }

    if (best && rel_out) {
        const char *rel = path + best_len;
        while (*rel == '/') rel++;
        *rel_out = rel;
    }
    return best;
}

static const char *path_next_component(const char *src, char *dst, size_t maxlen) {
    while (*src == '/') src++;
    size_t i = 0;
    while (*src && *src != '/' && i < maxlen - 1)
        dst[i++] = *src++;
    dst[i] = '\0';
    return src;
}

int vfs_lookup(const char *path, vnode_t **out) {
    if (!path || !out)  return -EINVAL;
    if (!g_vfs_ready)   return -EIO;
    if (path[0] != '/') return -EINVAL;

    const char *rel;
    vfs_mount_t *mnt = find_mount(path, &rel);
    if (!mnt) {
        serial_printf("[VFS] lookup '%s': no mount found\n", path);
        return -ENOENT;
    }

    vnode_t *cur = mnt->root;
    if (!cur) {
        serial_printf("[VFS] lookup '%s': mount root is NULL!\n", path);
        return -EIO;
    }
    vnode_ref(cur);

    if (*rel == '\0') {
        *out = cur;
        return 0;
    }

    char comp[VFS_MAX_NAME];
    const char *p = rel;
    while (*p) {
        p = path_next_component(p, comp, sizeof(comp));
        if (comp[0] == '\0') continue;

        if (cur->type != VFS_NODE_DIR) {
            serial_printf("[VFS] lookup '%s': '%s' is not a dir (type=%d)\n",
                          path, comp, cur->type);
            vnode_unref(cur);
            return -ENOTDIR;
        }
        if (!cur->ops || !cur->ops->lookup) {
            serial_printf("[VFS] lookup '%s': no lookup op for comp '%s'\n", path, comp);
            vnode_unref(cur);
            return -EIO;
        }

        vnode_t *next = NULL;
        int ret = cur->ops->lookup(cur, comp, &next);
        if (ret < 0) {
            serial_printf("[VFS] lookup '%s': ops->lookup('%s') = %d\n", path, comp, ret);
            vnode_unref(cur);
            return ret;
        }

        vnode_unref(cur);
        cur = next;

        if (cur->mounted) {
            vnode_t *mroot = cur->mounted->root;
            vnode_ref(mroot);
            vnode_unref(cur);
            cur = mroot;
        }
    }

    *out = cur;
    return 0;
}

vfs_file_t *vfs_file_alloc(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (g_open_files[i].refcount == 0) {
            memset(&g_open_files[i], 0, sizeof(vfs_file_t));
            g_open_files[i].refcount = 1;
            return &g_open_files[i];
        }
    }
    return NULL;
}

void vfs_file_free(vfs_file_t *file) {
    if (!file) return;
    int old = __atomic_fetch_sub(&file->refcount, 1, __ATOMIC_ACQ_REL);
    if (old == 1) {
        vnode_unref(file->vnode);
        file->vnode = NULL;
    }
}

int vfs_open(const char *path, int flags, uint32_t mode, vfs_file_t **out) {
    if (!path || !out) return -EINVAL;

    vnode_t *node = NULL;
    int ret = vfs_lookup(path, &node);

    if (ret == -ENOENT && (flags & O_CREAT)) {
        char dirpath[VFS_MAX_PATH];
        strncpy(dirpath, path, VFS_MAX_PATH - 1);
        dirpath[VFS_MAX_PATH - 1] = '\0';

        char *slash = NULL;
        for (int i = (int)strlen(dirpath) - 1; i >= 0; i--) {
            if (dirpath[i] == '/') { slash = &dirpath[i]; break; }
        }
        if (!slash) return -EINVAL;

        char filename[VFS_MAX_NAME];
        strncpy(filename, slash + 1, VFS_MAX_NAME - 1);
        filename[VFS_MAX_NAME - 1] = '\0';

        if (filename[0] == '\0') return -EINVAL;

        if (slash == dirpath)
            dirpath[1] = '\0';
        else
            *slash = '\0';

        serial_printf("[VFS] open O_CREAT: parent='%s' name='%s'\n", dirpath, filename);

        vnode_t *dir = NULL;
        ret = vfs_lookup(dirpath, &dir);
        if (ret < 0) {
            serial_printf("[VFS] open O_CREAT: lookup parent '%s' failed: %d\n", dirpath, ret);
            return ret;
        }

        if (!dir->ops || !dir->ops->create) {
            serial_printf("[VFS] open O_CREAT: parent has no create op\n");
            vnode_unref(dir);
            return -ENOSYS;
        }
        ret = dir->ops->create(dir, filename, mode, &node);
        vnode_unref(dir);
        if (ret < 0) {
            serial_printf("[VFS] open O_CREAT: create '%s' failed: %d\n", filename, ret);
            return ret;
        }
    } else if (ret < 0) {
        return ret;
    }

    int acc = flags & O_ACCMODE;
    if ((acc == O_WRONLY || acc == O_RDWR) && node->type == VFS_NODE_DIR) {
        vnode_unref(node);
        return -EISDIR;
    }

    vfs_file_t *file = vfs_file_alloc();
    if (!file) {
        vnode_unref(node);
        return -ENFILE;
    }

    file->vnode  = node;
    file->flags  = flags;
    file->offset = (flags & O_APPEND) ? node->size : 0;

    if ((flags & O_TRUNC) && (acc == O_WRONLY || acc == O_RDWR)) {
        if (node->ops && node->ops->truncate) {
            node->ops->truncate(node, 0);
        } else {
            node->size = 0;
        }
        file->offset = 0;
    }

    *out = file;
    return 0;
}

void vfs_close(vfs_file_t *file) {
    vfs_file_free(file);
}

int64_t vfs_read(vfs_file_t *file, void *buf, size_t len) {
    if (!file || !file->vnode) return -EBADF;
    if (len == 0) return 0;
    if ((file->flags & O_ACCMODE) == O_WRONLY) return -EBADF;
    if (!file->vnode->ops || !file->vnode->ops->read) return -EIO;

    int64_t n = file->vnode->ops->read(file->vnode, buf, len, file->offset);
    if (n > 0) file->offset += (uint64_t)n;
    return n;
}

int64_t vfs_write(vfs_file_t *file, const void *buf, size_t len) {
    if (!file || !file->vnode) return -EBADF;
    if (len == 0) return 0;
    if ((file->flags & O_ACCMODE) == O_RDONLY) return -EBADF;
    if (!file->vnode->ops || !file->vnode->ops->write) return -EIO;

    if (file->flags & O_APPEND) file->offset = file->vnode->size;

    int64_t n = file->vnode->ops->write(file->vnode, buf, len, file->offset);
    if (n > 0) file->offset += (uint64_t)n;
    return n;
}

int64_t vfs_seek(vfs_file_t *file, int64_t offset, int whence) {
    if (!file || !file->vnode) return -EBADF;
    vnode_type_t t = file->vnode->type;
    if (t == VFS_NODE_CHARDEV || t == VFS_NODE_PIPE) return -ESPIPE;

    int64_t new_off;
    switch (whence) {
    case SEEK_SET: new_off = offset; break;
    case SEEK_CUR: new_off = (int64_t)file->offset + offset; break;
    case SEEK_END: new_off = (int64_t)file->vnode->size + offset; break;
    default: return -EINVAL;
    }
    if (new_off < 0) return -EINVAL;
    file->offset = (uint64_t)new_off;
    return new_off;
}

int vfs_stat(const char *path, vfs_stat_t *out) {
    if (!path || !out) return -EINVAL;
    vnode_t *node = NULL;
    int ret = vfs_lookup(path, &node);
    if (ret < 0) return ret;

    if (node->ops && node->ops->stat) {
        ret = node->ops->stat(node, out);
    } else {
        memset(out, 0, sizeof(*out));
        out->st_ino  = node->ino;
        out->st_type = node->type;
        out->st_mode = node->mode;
        out->st_uid  = node->uid;
        out->st_gid  = node->gid;
        out->st_size = node->size;
        ret = 0;
    }
    vnode_unref(node);
    return ret;
}

int vfs_fstat(vfs_file_t *file, vfs_stat_t *out) {
    if (!file || !file->vnode || !out) return -EBADF;
    vnode_t *node = file->vnode;
    if (node->ops && node->ops->stat) return node->ops->stat(node, out);
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = node->type;
    out->st_mode = node->mode;
    out->st_uid  = node->uid;
    out->st_gid  = node->gid;
    out->st_size = node->size;
    return 0;
}

int64_t vfs_ioctl(vfs_file_t *file, uint64_t req, void *arg) {
    if (!file || !file->vnode) return -EBADF;
    if (!file->vnode->ops || !file->vnode->ops->ioctl) return -ENOTTY;
    return file->vnode->ops->ioctl(file->vnode, req, arg);
}

int vfs_readdir(vfs_file_t *file, vfs_dirent_t *out) {
    if (!file || !file->vnode || !out) return -EBADF;
    if (file->vnode->type != VFS_NODE_DIR) return -ENOTDIR;
    if (!file->vnode->ops || !file->vnode->ops->readdir) return -EIO;
    int ret = file->vnode->ops->readdir(file->vnode, file->offset, out);
    if (ret == 0) file->offset++;
    return ret;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    if (!path) return -EINVAL;
    char dirpath[VFS_MAX_PATH];
    strncpy(dirpath, path, VFS_MAX_PATH - 1);
    dirpath[VFS_MAX_PATH - 1] = '\0';

    char *slash = NULL;
    for (int i = (int)strlen(dirpath) - 1; i >= 0; i--) {
        if (dirpath[i] == '/') { slash = &dirpath[i]; break; }
    }
    if (!slash) return -EINVAL;

    char dirname[VFS_MAX_NAME];
    strncpy(dirname, slash + 1, VFS_MAX_NAME - 1);
    dirname[VFS_MAX_NAME - 1] = '\0';

    if (dirname[0] == '\0') return -EINVAL;

    if (slash == dirpath) dirpath[1] = '\0';
    else                  *slash     = '\0';

    vnode_t *dir = NULL;
    int ret = vfs_lookup(dirpath, &dir);
    if (ret < 0) return ret;
    if (!dir->ops || !dir->ops->mkdir) { vnode_unref(dir); return -ENOSYS; }
    ret = dir->ops->mkdir(dir, dirname, mode);
    vnode_unref(dir);
    return ret;
}

fd_table_t *fd_table_create(void) {
    fd_table_t *t = kzalloc(sizeof(fd_table_t));
    return t;
}

fd_table_t *fd_table_clone(const fd_table_t *src) {
    if (!src) return NULL;
    fd_table_t *dst = kzalloc(sizeof(fd_table_t));
    if (!dst) return NULL;
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (src->entries[i].file) {
            dst->entries[i] = src->entries[i];
            __atomic_fetch_add(&dst->entries[i].file->refcount, 1, __ATOMIC_RELAXED);
        }
    }
    return dst;
}

void fd_table_cloexec(fd_table_t *table) {
    if (!table) return;
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (table->entries[i].file && (table->entries[i].fd_flags & FD_CLOEXEC)) {
            vfs_file_free(table->entries[i].file);
            table->entries[i].file     = NULL;
            table->entries[i].fd_flags = 0;
        }
    }
}

void fd_table_destroy(fd_table_t *table) {
    if (!table) return;
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (table->entries[i].file) {
            vfs_file_free(table->entries[i].file);
            table->entries[i].file = NULL;
        }
    }
    kfree(table);
}

int fd_alloc(fd_table_t *table, vfs_file_t *file, int min_fd) {
    if (!table || !file) return -EINVAL;
    if (min_fd < 0 || min_fd >= TASK_MAX_FDS) return -EINVAL;
    for (int i = min_fd; i < TASK_MAX_FDS; i++) {
        if (!table->entries[i].file) {
            table->entries[i].file     = file;
            table->entries[i].fd_flags = 0;
            return i;
        }
    }
    return -EMFILE;
}

vfs_file_t *fd_get(const fd_table_t *table, int fd) {
    if (!table || fd < 0 || fd >= TASK_MAX_FDS) return NULL;
    return table->entries[fd].file;
}

int fd_close(fd_table_t *table, int fd) {
    if (!table || fd < 0 || fd >= TASK_MAX_FDS) return -EBADF;
    vfs_file_t *file = table->entries[fd].file;
    if (!file) return -EBADF;
    vfs_file_free(file);
    table->entries[fd].file     = NULL;
    table->entries[fd].fd_flags = 0;
    return 0;
}

int fd_dup2(fd_table_t *table, int oldfd, int newfd) {
    if (!table) return -EBADF;
    if (oldfd < 0 || oldfd >= TASK_MAX_FDS) return -EBADF;
    if (newfd < 0 || newfd >= TASK_MAX_FDS) return -EBADF;
    if (oldfd == newfd) return newfd;

    vfs_file_t *src = table->entries[oldfd].file;
    if (!src) return -EBADF;

    if (table->entries[newfd].file)
        vfs_file_free(table->entries[newfd].file);

    table->entries[newfd].file     = src;
    table->entries[newfd].fd_flags = 0;
    __atomic_fetch_add(&src->refcount, 1, __ATOMIC_RELAXED);
    return newfd;
}

int fd_set_flags(fd_table_t *table, int fd, int flags) {
    if (!table || fd < 0 || fd >= TASK_MAX_FDS) return -EBADF;
    if (!table->entries[fd].file) return -EBADF;
    table->entries[fd].fd_flags = flags;
    return 0;
}

int fd_get_flags(const fd_table_t *table, int fd) {
    if (!table || fd < 0 || fd >= TASK_MAX_FDS) return -EBADF;
    if (!table->entries[fd].file) return -EBADF;
    return table->entries[fd].fd_flags;
}

int vfs_init_stdio(void *task_ptr) {
    task_t *t = (task_t *)task_ptr;
    if (!t || !t->fd_table) return -EINVAL;

    vfs_file_t *in = NULL;
    int ret = vfs_open("/dev/tty", O_RDONLY, 0, &in);
    if (ret < 0) return ret;
    fd_alloc(t->fd_table, in, 0);

    vfs_file_t *out = NULL;
    ret = vfs_open("/dev/tty", O_WRONLY, 0, &out);
    if (ret < 0) return ret;
    fd_alloc(t->fd_table, out, 1);

    vfs_file_t *err = NULL;
    ret = vfs_open("/dev/tty", O_WRONLY, 0, &err);
    if (ret < 0) return ret;
    fd_alloc(t->fd_table, err, 2);

    return 0;
}