#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

#define O_RDONLY     0x000
#define O_WRONLY     0x001
#define O_RDWR       0x002
#define O_ACCMODE    0x003
#define O_CREAT      0x040
#define O_TRUNC      0x200
#define O_APPEND     0x400
#define O_NONBLOCK   0x800
#define O_DIRECTORY  0x10000
#define O_CLOEXEC    0x80000

#define F_GETFD      1
#define F_SETFD      2
#define F_GETFL      3
#define F_SETFL      4
#define FD_CLOEXEC   1

int open(const char *path, int flags, ...);
int fcntl(int fd, int cmd, ...);
int creat(const char *path, mode_t mode);

#endif
