#ifndef _FCNTL_H
#define _FCNTL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define O_RDONLY     0x000
#define O_WRONLY     0x001
#define O_RDWR       0x002
#define O_ACCMODE    0x003
#define O_CREAT      0x040
#define O_EXCL       0x080
#define O_NOFOLLOW   0x20000
#define O_TRUNC      0x200
#define O_APPEND     0x400
#define O_NONBLOCK   0x800
#define O_DIRECTORY  0x10000
#define O_CLOEXEC    0x80000

#define F_DUPFD      0
#define F_DUPFD_CLOEXEC 1030
#define F_GETFD      1
#define F_SETFD      2
#define F_GETFL      3
#define F_SETFL      4
#define F_GETLK      5
#define F_SETLK      6
#define F_SETLKW     7
#define F_ADD_SEALS  1033
#define F_GET_SEALS  1034

#define F_SEAL_SEAL   0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW   0x0004
#define F_SEAL_WRITE  0x0008
#define FD_CLOEXEC   1

#define F_RDLCK      0
#define F_WRLCK      1
#define F_UNLCK      2

struct flock {
    short l_type;
    short l_whence;
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
};

int open(const char *path, int flags, ...);
int fcntl(int fd, int cmd, ...);
int creat(const char *path, mode_t mode);

#ifdef __cplusplus
}
#endif
#endif
