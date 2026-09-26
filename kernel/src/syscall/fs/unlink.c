#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

static int dir_is_empty(vnode_t *dir)
{
    if (!dir->ops || !dir->ops->readdir) return 1;
    vfs_dirent_t d;
    for (uint64_t i = 0; ; i++) {
        if (dir->ops->readdir(dir, i, &d) < 0) break;
        if (!strcmp(d.d_name, ".") || !strcmp(d.d_name, "..")) continue;
        if (d.d_name[0]) return 0;
    }
    return 1;
}

static int check_target(const char *path, int want_dir)
{
    vnode_t *node = NULL;
    int r = vfs_lookup_nofollow(path, &node);
    if (r < 0) return r;

    int is_dir = (node->type == VFS_NODE_DIR);
    if (want_dir && !is_dir)  { vnode_unref(node); return -ENOTDIR; }
    if (!want_dir && is_dir)  { vnode_unref(node); return -EISDIR; }
    if (want_dir && !dir_is_empty(node)) { vnode_unref(node); return -ENOTEMPTY; }

    vnode_unref(node);
    return 0;
}

static int64_t remove_entry(uint64_t path_ptr, int want_dir)
{
    char path[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(path, (const char *)path_ptr, sizeof(path));
    if (rp < 0) return rp;

    int ct = check_target(path, want_dir);
    if (ct < 0) return ct;
    int pp = syscall_perm_parent(path, 2);
    if (pp < 0) return pp;
    int ps = syscall_perm_sticky(path);
    if (ps < 0) return ps;
    char dirpath[VFS_MAX_PATH];
    strncpy(dirpath, path, 255);
    char *slash = NULL;
    for (int i = (int)strlen(dirpath) - 1; i >= 0; i--) {
        if (dirpath[i] == '/') { slash = &dirpath[i]; break; }
    }
    if (!slash) return -EINVAL;
    char name[256];
    strncpy(name, slash + 1, 255);
    if (slash == dirpath) dirpath[1] = '\0';
    else                  *slash = '\0';
    vnode_t *dir = NULL;
    int r = vfs_lookup(dirpath, &dir);
    if (r < 0) return r;
    if (!dir->ops || !dir->ops->unlink) { vnode_unref(dir); return -ENOSYS; }
    r = dir->ops->unlink(dir, name);
    vnode_unref(dir);
    return r;
}

int64_t sys_unlink(uint64_t path_ptr, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return remove_entry(path_ptr, 0);
}

int64_t sys_rmdir(uint64_t p, uint64_t a2, uint64_t a3,
                  uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return remove_entry(p, 1);
}
