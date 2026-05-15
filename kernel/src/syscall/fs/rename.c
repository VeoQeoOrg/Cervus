#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

int64_t sys_rename(uint64_t old_ptr, uint64_t new_ptr, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    char oldp[256], newp[256];
    if (syscall_strncpy_from_user(oldp, (const char *)old_ptr, 256) < 0) return -EFAULT;
    if (syscall_strncpy_from_user(newp, (const char *)new_ptr, 256) < 0) return -EFAULT;

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
                if (newp[dlen - 1] != '/') { newp[dlen] = '/'; newp[dlen + 1] = '\0'; dlen++; }
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

    char dirp[256];
    strncpy(dirp, oldp, 255);
    dirp[255] = '\0';
    char *sl = NULL;
    for (int i = (int)strlen(dirp) - 1; i >= 0; i--)
        if (dirp[i] == '/') { sl = &dirp[i]; break; }
    if (sl) {
        char nm[256];
        strncpy(nm, sl + 1, 255);
        if (sl == dirp) dirp[1] = '\0';
        else            *sl = '\0';
        vnode_t *dir = NULL;
        if (vfs_lookup(dirp, &dir) == 0) {
            if (dir->ops && dir->ops->unlink) dir->ops->unlink(dir, nm);
            vnode_unref(dir);
        }
    }
    vfs_sync_all();
    return 0;
}
