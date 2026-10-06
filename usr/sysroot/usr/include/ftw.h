#ifndef _FTW_H
#define _FTW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/stat.h>

#define FTW_F   0
#define FTW_D   1
#define FTW_DNR 2
#define FTW_NS  3
#define FTW_SL  4
#define FTW_DP  5
#define FTW_SLN 6

#define FTW_PHYS  1
#define FTW_MOUNT 2
#define FTW_CHDIR 4
#define FTW_DEPTH 8

struct FTW {
    int base;
    int level;
};

int ftw(const char *dir, int (*fn)(const char *, const struct stat *, int), int fd_limit);
int nftw(const char *dir, int (*fn)(const char *, const struct stat *, int, struct FTW *), int fd_limit, int flags);

#ifdef __cplusplus
}
#endif
#endif
