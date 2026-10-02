#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

int64_t rename_kernel_paths(const char *oldp, const char *newp)
{
    int ps = syscall_perm_parent(oldp, 2);
    if (ps < 0) return ps;
    int pd = syscall_perm_parent(newp, 2);
    if (pd < 0) return pd;
    int ss = syscall_perm_sticky(oldp);
    if (ss < 0) return ss;
    vnode_t *probe = NULL;
    if (vfs_lookup_nofollow(newp, &probe) == 0) {
        vnode_unref(probe);
        int sd = syscall_perm_sticky(newp);
        if (sd < 0) return sd;
    }
    return vfs_rename(oldp, newp);
}

int64_t link_kernel_paths(const char *oldp, const char *newp, int follow)
{
    int pd = syscall_perm_parent(newp, 2);
    if (pd < 0) return pd;
    if (follow) {
        vnode_t *v = NULL;
        int r = vfs_lookup(oldp, &v);
        if (r < 0) return r;
        vnode_unref(v);
    }
    return vfs_link(oldp, newp);
}

int64_t sys_rename(uint64_t old_ptr, uint64_t new_ptr, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    char oldp[VFS_MAX_PATH], newp[VFS_MAX_PATH];
    int rp1 = syscall_resolve_path_from_user(oldp, (const char *)old_ptr, sizeof(oldp));
    if (rp1 < 0) return rp1;
    int rp2 = syscall_resolve_path_from_user(newp, (const char *)new_ptr, sizeof(newp));
    if (rp2 < 0) return rp2;
    return rename_kernel_paths(oldp, newp);
}

int64_t sys_link(uint64_t old_ptr, uint64_t new_ptr)
{
    char oldp[VFS_MAX_PATH], newp[VFS_MAX_PATH];
    int rp1 = syscall_resolve_path_from_user(oldp, (const char *)old_ptr, sizeof(oldp));
    if (rp1 < 0) return rp1;
    int rp2 = syscall_resolve_path_from_user(newp, (const char *)new_ptr, sizeof(newp));
    if (rp2 < 0) return rp2;
    return link_kernel_paths(oldp, newp, 0);
}
