#ifndef _CERVUS_KSTAT_H
#define _CERVUS_KSTAT_H

#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/abi.h>
#include <sys/syscall.h>
#include <errno.h>

typedef struct {
    uint64_t st_ino;
    uint32_t st_type;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint64_t st_blocks;
    int64_t  k_atime;
    int64_t  k_mtime;
    int64_t  k_ctime;
    uint64_t st_nlink;
    uint64_t st_dev;
    uint64_t st_blksize;
    uint64_t st_rdev;
} __cervus_kstat_t;

_Static_assert(sizeof(__cervus_kstat_t) == 96,
               "__cervus_kstat_t must match the kernel's vfs_stat_t");

static inline void __cervus_stat_from_kernel(struct stat *out, const __cervus_kstat_t *k)
{
    memset(out, 0, sizeof *out);
    out->st_ino     = k->st_ino;
    out->st_type    = k->st_type;
    out->st_mode    = k->st_mode;
    out->st_uid     = k->st_uid;
    out->st_gid     = k->st_gid;
    out->st_size    = (off_t)k->st_size;
    out->st_blocks  = k->st_blocks;
    out->st_nlink   = k->st_nlink;
    out->st_dev     = k->st_dev;
    out->st_blksize = k->st_blksize;
    out->st_rdev    = k->st_rdev;
    out->st_atim.tv_sec = k->k_atime;
    out->st_mtim.tv_sec = k->k_mtime;
    out->st_ctim.tv_sec = k->k_ctime;
}

#define __CERVUS_KSTAT_PATH     0
#define __CERVUS_KSTAT_NOFOLLOW 1
#define __CERVUS_KSTAT_FD       2

static inline long __cervus_kstat(long kind, uint64_t arg, __cervus_kstat_t *k)
{
    long r = (long)syscall3(SYS_KSTAT, kind, arg, k);
    if (r != -ENOSYS) return r;
    memset(k, 0, sizeof *k);
    long nr = kind == __CERVUS_KSTAT_FD ? SYS_FSTAT
            : kind == __CERVUS_KSTAT_NOFOLLOW ? SYS_LSTAT : SYS_STAT;
    return (long)syscall2(nr, arg, k);
}

#endif
