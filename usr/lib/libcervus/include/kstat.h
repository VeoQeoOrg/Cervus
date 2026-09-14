#ifndef _CERVUS_KSTAT_H
#define _CERVUS_KSTAT_H

#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

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
} __cervus_kstat_t;

_Static_assert(sizeof(__cervus_kstat_t) == 88,
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
    out->st_atim.tv_sec = k->k_atime;
    out->st_mtim.tv_sec = k->k_mtime;
    out->st_ctim.tv_sec = k->k_ctime;
}

#endif
