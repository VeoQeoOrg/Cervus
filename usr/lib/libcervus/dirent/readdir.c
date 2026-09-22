#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <dirent.h>

typedef struct {
    uint64_t d_ino;
    uint8_t  d_type;
    char     d_name[256];
} __kernel_dirent_t;

static uint8_t kernel_type_to_dt(uint8_t t)
{
    switch (t) {
        case 0:  return DT_REG;
        case 1:  return DT_DIR;
        case 2:  return DT_CHR;
        case 3:  return DT_BLK;
        case 4:  return DT_LNK;
        case 5:  return DT_FIFO;
        case 6:  return DT_SOCK;
        default: return DT_UNKNOWN;
    }
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp) return NULL;
    __kernel_dirent_t kde;
    int r = (int)syscall2(SYS_READDIR, dirp->fd, &kde);
    if (r != 0) return NULL;
    dirp->buf.d_ino  = kde.d_ino;
    dirp->buf.d_type = kernel_type_to_dt(kde.d_type);
    size_t nl = strlen(kde.d_name);
    if (nl >= sizeof(dirp->buf.d_name)) nl = sizeof(dirp->buf.d_name) - 1;
    memcpy(dirp->buf.d_name, kde.d_name, nl);
    dirp->buf.d_name[nl] = '\0';
    return &dirp->buf;
}
