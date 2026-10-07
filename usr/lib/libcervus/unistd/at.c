#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <kstat.h>

int openat(int dirfd, const char *path, int flags, ...)
{
    unsigned mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, unsigned);
        va_end(ap);
    }
    return (int)__cervus_sys_ret(syscall4(SYS_OPENAT, dirfd, path, flags, mode));
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
    return (int)__cervus_sys_ret(syscall3(SYS_MKDIRAT, dirfd, path, mode));
}

int unlinkat(int dirfd, const char *path, int flags)
{
    return (int)__cervus_sys_ret(syscall3(SYS_UNLINKAT, dirfd, path, flags));
}

int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    return (int)__cervus_sys_ret(syscall4(SYS_RENAMEAT, olddirfd, oldpath, newdirfd, newpath));
}

int renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned flags)
{
    if (flags) { __cervus_set_errno(EINVAL); return -1; }
    return renameat(olddirfd, oldpath, newdirfd, newpath);
}

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    return (int)__cervus_sys_ret(syscall5(SYS_LINKAT, olddirfd, oldpath, newdirfd, newpath, flags));
}

int symlinkat(const char *target, int dirfd, const char *linkpath)
{
    return (int)__cervus_sys_ret(syscall3(SYS_SYMLINKAT, target, dirfd, linkpath));
}

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t size)
{
    return (ssize_t)__cervus_sys_ret(syscall4(SYS_READLINKAT, dirfd, path, buf, size));
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    __cervus_kstat_t k;
    long r = (long)syscall4(SYS_FSTATAT, dirfd, path, &k, flags);
    if (r < 0 && r > -4096) { __cervus_set_errno((int)-r); return -1; }
    __cervus_stat_from_kernel(st, &k);
    return 0;
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    return (int)__cervus_sys_ret(syscall4(SYS_FCHMODAT, dirfd, path, mode, flags));
}

int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
    return (int)__cervus_sys_ret(syscall5(SYS_FCHOWNAT, dirfd, path, owner, group, flags));
}

int faccessat(int dirfd, const char *path, int mode, int flags)
{
    struct stat st;
    if (fstatat(dirfd, path, &st, flags & AT_SYMLINK_NOFOLLOW) < 0) return -1;
    if (mode == F_OK) return 0;
    uid_t uid = (flags & AT_EACCESS) ? geteuid() : getuid();
    if (uid == 0) {
        if ((mode & X_OK) && !(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) && st.st_type != 1) {
            __cervus_set_errno(EACCES);
            return -1;
        }
        return 0;
    }
    unsigned bits;
    if (uid == st.st_uid) bits = (st.st_mode >> 6) & 7;
    else if (getgid() == st.st_gid) bits = (st.st_mode >> 3) & 7;
    else bits = st.st_mode & 7;
    if (((mode & R_OK) && !(bits & 4)) || ((mode & W_OK) && !(bits & 2)) || ((mode & X_OK) && !(bits & 1))) {
        __cervus_set_errno(EACCES);
        return -1;
    }
    return 0;
}

int fchdir(int fd)
{
    return (int)__cervus_sys_ret(syscall1(SYS_FCHDIR, fd));
}
