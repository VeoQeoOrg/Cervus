#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/memory/pmm.h"
#include <string.h>

#define AT_FDCWD_K            (-100)
#define AT_SYMLINK_NOFOLLOW_K 0x100
#define AT_REMOVEDIR_K        0x200
#define AT_SYMLINK_FOLLOW_K   0x400
#define AT_EMPTY_PATH_K       0x1000

extern int64_t sys_open(uint64_t, uint64_t, uint64_t);
extern int64_t sys_mkdir(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_unlink(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_rmdir(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t sys_symlink(uint64_t, uint64_t);
extern int64_t sys_mknod(uint64_t, uint64_t, uint64_t);
extern int64_t sys_readlink(uint64_t, uint64_t, uint64_t);
extern int64_t sys_kstat(uint64_t, uint64_t, uint64_t);
extern int64_t sys_chmod(uint64_t, uint64_t);
extern int64_t sys_chown(uint64_t, uint64_t, uint64_t);
extern int64_t sys_lchown(uint64_t, uint64_t, uint64_t);
extern int64_t sys_utimes(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int64_t rename_kernel_paths(const char *oldp, const char *newp);
extern int64_t link_kernel_paths(const char *oldp, const char *newp, int follow);

static int at_enter(task_t *t, int64_t dirfd, uint64_t upath) {
    if ((int)dirfd == AT_FDCWD_K) return 0;
    char first = 0;
    if (upath && syscall_copy_from_user(&first, (const void *)upath, 1) == 0 && first == '/')
        return 0;
    if (!t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)dirfd);
    if (!f) return -EBADF;
    if (!f->path || !f->vnode || f->vnode->type != VFS_NODE_DIR) { fd_put(f); return -ENOTDIR; }
    size_t n = strlen(f->path);
    char *base = kmalloc(n + 1);
    if (!base) { fd_put(f); return -ENOMEM; }
    memcpy(base, f->path, n + 1);
    fd_put(f);
    t->at_base = base;
    return 0;
}

static void at_leave(task_t *t) {
    if (t->at_base) { kfree(t->at_base); t->at_base = NULL; }
}

static int resolve_at(task_t *t, int64_t dirfd, uint64_t upath, char *out, size_t cap) {
    int r = at_enter(t, dirfd, upath);
    if (r < 0) return r;
    r = syscall_resolve_path_from_user(out, (const char *)upath, cap);
    at_leave(t);
    return r;
}

#define AT_CALL(expr) do { \
    task_t *t_ = syscall_cur_task(); \
    if (!t_) return -ESRCH; \
    int e_ = at_enter(t_, dirfd, path); \
    if (e_ < 0) return e_; \
    int64_t r_ = (expr); \
    at_leave(t_); \
    return r_; \
} while (0)

int64_t sys_openat(uint64_t dirfd, uint64_t path, uint64_t flags, uint64_t mode) {
    AT_CALL(sys_open(path, flags, mode));
}

int64_t sys_mkdirat(uint64_t dirfd, uint64_t path, uint64_t mode) {
    AT_CALL(sys_mkdir(path, mode, 0, 0, 0, 0));
}

int64_t sys_mknodat(uint64_t dirfd, uint64_t path, uint64_t mode, uint64_t dev) {
    AT_CALL(sys_mknod(path, mode, dev));
}

int64_t sys_unlinkat(uint64_t dirfd, uint64_t path, uint64_t flags) {
    if (flags & ~(uint64_t)AT_REMOVEDIR_K) return -EINVAL;
    if (flags & AT_REMOVEDIR_K) AT_CALL(sys_rmdir(path, 0, 0, 0, 0, 0));
    AT_CALL(sys_unlink(path, 0, 0, 0, 0, 0));
}

int64_t sys_symlinkat(uint64_t target, uint64_t dirfd, uint64_t path) {
    AT_CALL(sys_symlink(target, path));
}

int64_t sys_readlinkat(uint64_t dirfd, uint64_t path, uint64_t buf, uint64_t size) {
    AT_CALL(sys_readlink(path, buf, size));
}

int64_t sys_fstatat(uint64_t dirfd, uint64_t path, uint64_t statbuf, uint64_t flags) {
    if (flags & AT_EMPTY_PATH_K) {
        char first = 1;
        if (!path || (syscall_copy_from_user(&first, (const void *)path, 1) == 0 && first == 0)) {
            if ((int)dirfd == AT_FDCWD_K) {
                task_t *t = syscall_cur_task();
                if (!t) return -ESRCH;
                char cwd[VFS_MAX_PATH];
                strncpy(cwd, t->cwd[0] ? t->cwd : "/", sizeof cwd - 1);
                cwd[sizeof cwd - 1] = 0;
                vfs_stat_t st;
                int r = vfs_stat(cwd, &st);
                if (r < 0) return r;
                return syscall_copy_to_user((void *)statbuf, &st, sizeof st);
            }
            return sys_kstat(2, dirfd, statbuf);
        }
    }
    AT_CALL(sys_kstat((flags & AT_SYMLINK_NOFOLLOW_K) ? 1 : 0, path, statbuf));
}

int64_t sys_fchmodat(uint64_t dirfd, uint64_t path, uint64_t mode, uint64_t flags) {
    (void)flags;
    AT_CALL(sys_chmod(path, mode));
}

int64_t sys_fchownat(uint64_t dirfd, uint64_t path, uint64_t uid, uint64_t gid, uint64_t flags) {
    if (flags & AT_SYMLINK_NOFOLLOW_K) AT_CALL(sys_lchown(path, uid, gid));
    AT_CALL(sys_chown(path, uid, gid));
}

int64_t sys_utimensat(uint64_t dirfd, uint64_t path, uint64_t atime, uint64_t mtime, uint64_t flags) {
    (void)flags;
    AT_CALL(sys_utimes(path, atime, mtime, 0, 0, 0));
}

int64_t sys_renameat(uint64_t olddirfd, uint64_t oldpath, uint64_t newdirfd, uint64_t newpath) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    char oldp[VFS_MAX_PATH], newp[VFS_MAX_PATH];
    int r = resolve_at(t, (int64_t)olddirfd, oldpath, oldp, sizeof oldp);
    if (r < 0) return r;
    r = resolve_at(t, (int64_t)newdirfd, newpath, newp, sizeof newp);
    if (r < 0) return r;
    return rename_kernel_paths(oldp, newp);
}

int64_t sys_linkat(uint64_t olddirfd, uint64_t oldpath, uint64_t newdirfd, uint64_t newpath, uint64_t flags) {
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    char oldp[VFS_MAX_PATH], newp[VFS_MAX_PATH];
    int r = resolve_at(t, (int64_t)olddirfd, oldpath, oldp, sizeof oldp);
    if (r < 0) return r;
    r = resolve_at(t, (int64_t)newdirfd, newpath, newp, sizeof newp);
    if (r < 0) return r;
    return link_kernel_paths(oldp, newp, (flags & AT_SYMLINK_FOLLOW_K) != 0);
}

int64_t sys_fchdir(uint64_t fd) {
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    int r = 0;
    if (!f->path || !f->vnode || f->vnode->type != VFS_NODE_DIR) r = -ENOTDIR;
    else {
        size_t n = strlen(f->path);
        if (n >= sizeof t->cwd) r = -ENAMETOOLONG;
        else memcpy(t->cwd, f->path, n + 1);
    }
    fd_put(f);
    return r;
}
