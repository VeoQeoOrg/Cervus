#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#ifndef __ssize_t_defined
#define __ssize_t_defined
typedef long ssize_t;
#endif

typedef int32_t  pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t id_t;
typedef int64_t  off_t;
typedef int64_t  off64_t;
typedef uint64_t ino_t;
typedef uint32_t mode_t;
typedef uint64_t dev_t;
typedef uint64_t blkcnt_t;
typedef uint64_t fsblkcnt_t;
typedef uint64_t fsfilcnt_t;
typedef uint64_t blksize_t;
typedef int64_t  time_t;
typedef int      clockid_t;
typedef void    *timer_t;
typedef uint64_t nlink_t;
typedef long     suseconds_t;
typedef unsigned useconds_t;
typedef long     register_t;
typedef int64_t  loff_t;
typedef char    *caddr_t;

#ifndef __CERVUS_KEY_T
#define __CERVUS_KEY_T
typedef int key_t;
#endif

#ifndef __CERVUS_CLOCK_T
#define __CERVUS_CLOCK_T
typedef long clock_t;
#endif

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
typedef unsigned short ushort;
typedef unsigned int   uint;
typedef unsigned long  ulong;
typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

#ifdef __cplusplus
}
#endif

#include <sys/select.h>

#endif
