#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_symlink(uint64_t target_ptr, uint64_t link_ptr)
{
    char ktarget[VFS_MAX_PATH];
    if (syscall_strncpy_from_user(ktarget, (const char *)target_ptr, sizeof(ktarget)) < 0)
        return -EFAULT;

    char klink[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(klink, (const char *)link_ptr, sizeof(klink));
    if (rp < 0) return rp;

    return vfs_symlink(ktarget, klink);
}

int64_t sys_readlink(uint64_t path_ptr, uint64_t buf_ptr, uint64_t bufsiz)
{
    if (!buf_ptr || bufsiz == 0) return -EINVAL;
    if (bufsiz > 4096) bufsiz = 4096;
    if (!syscall_uptr_validate((void *)buf_ptr, bufsiz)) return -EFAULT;

    char kpath[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (rp < 0) return rp;

    char kbuf[4096];
    int64_t n = vfs_readlink(kpath, kbuf, bufsiz);
    if (n < 0) return n;
    if ((size_t)n > bufsiz) n = bufsiz;
    if (syscall_copy_to_user((void *)buf_ptr, kbuf, (size_t)n) < 0) return -EFAULT;
    return n;
}
