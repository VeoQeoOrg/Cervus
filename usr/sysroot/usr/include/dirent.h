#ifndef _DIRENT_H
#define _DIRENT_H

#include <sys/types.h>

#define DT_UNKNOWN  0
#define DT_FILE     0
#define DT_REG      0
#define DT_DIR      1
#define DT_CHR      2
#define DT_BLK      3
#define DT_LNK      4
#define DT_PIPE     5
#define DT_FIFO     5

struct dirent {
    ino_t   d_ino;
    uint8_t d_type;
    char    d_name[256];
};

typedef struct __cervus_DIR DIR;

DIR           *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);
int            dirfd(DIR *dirp);

#endif
