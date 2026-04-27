#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/syscall.h>

typedef struct {
    uint64_t d_ino;
    uint8_t  d_type;
    char     d_name[256];
} __kernel_dirent_t;

struct __cervus_DIR {
    int fd;
    struct dirent buf;
};

DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return NULL;
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    return d;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp) return NULL;
    __kernel_dirent_t kde;
    int r = (int)syscall2(SYS_READDIR, dirp->fd, &kde);
    if (r != 0) return NULL;
    dirp->buf.d_ino  = kde.d_ino;
    dirp->buf.d_type = kde.d_type;
    size_t nl = strlen(kde.d_name);
    if (nl >= sizeof(dirp->buf.d_name)) nl = sizeof(dirp->buf.d_name) - 1;
    memcpy(dirp->buf.d_name, kde.d_name, nl);
    dirp->buf.d_name[nl] = '\0';
    return &dirp->buf;
}

int closedir(DIR *dirp)
{
    if (!dirp) return -1;
    int fd = dirp->fd;
    free(dirp);
    return close(fd);
}

void rewinddir(DIR *dirp)
{
    if (!dirp) return;
    lseek(dirp->fd, 0, SEEK_SET);
}

int dirfd(DIR *dirp) { return dirp ? dirp->fd : -1; }