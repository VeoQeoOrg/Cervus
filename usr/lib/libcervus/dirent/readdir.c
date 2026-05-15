#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <libcervus.h>

typedef struct {
    uint64_t d_ino;
    uint8_t  d_type;
    char     d_name[256];
} __kernel_dirent_t;

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
