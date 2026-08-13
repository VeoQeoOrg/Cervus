#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifndef __ssize_t_defined
#define __ssize_t_defined
typedef long ssize_t;
#endif

typedef int32_t  pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t  off_t;
typedef uint64_t ino_t;
typedef uint32_t mode_t;
typedef uint32_t dev_t;
typedef uint64_t blkcnt_t;
typedef uint64_t blksize_t;
typedef int64_t  time_t;

#endif
