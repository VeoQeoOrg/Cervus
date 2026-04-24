#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

#define S_IFMT    0170000
#define S_IFREG   0100000
#define S_IFDIR   0040000
#define S_IFCHR   0020000
#define S_IFBLK   0060000
#define S_IFIFO   0010000
#define S_IFLNK   0120000
#define S_IFSOCK  0140000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

#define S_IRWXU   00700
#define S_IRUSR   00400
#define S_IWUSR   00200
#define S_IXUSR   00100
#define S_IRWXG   00070
#define S_IRGRP   00040
#define S_IWGRP   00020
#define S_IXGRP   00010
#define S_IRWXO   00007
#define S_IROTH   00004
#define S_IWOTH   00002
#define S_IXOTH   00001

struct stat {
    ino_t    st_ino;
    uint32_t st_type;
    mode_t   st_mode;
    uid_t    st_uid;
    gid_t    st_gid;
    off_t    st_size;
    blkcnt_t st_blocks;
};

int stat(const char *path, struct stat *out);
int fstat(int fd, struct stat *out);
int mkdir(const char *path, mode_t mode);
int chmod(const char *path, mode_t mode);

#endif
