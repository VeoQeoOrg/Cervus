#ifndef _DIRENT_H
#define _DIRENT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12
#define DT_WHT      14

#define DT_FILE     DT_REG
#define DT_PIPE     DT_FIFO

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
DIR           *fdopendir(int fd);
int            scandir(const char *path, struct dirent ***namelist,
                       int (*filter)(const struct dirent *),
                       int (*compar)(const struct dirent **, const struct dirent **));
int            alphasort(const struct dirent **a, const struct dirent **b);
int            versionsort(const struct dirent **a, const struct dirent **b);

#ifdef __cplusplus
}
#endif
#endif
