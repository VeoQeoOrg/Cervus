#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#include <stdint.h>

typedef int8_t   __s8;
typedef uint8_t  __u8;
typedef int16_t  __s16;
typedef uint16_t __u16;
typedef int32_t  __s32;
typedef uint32_t __u32;
typedef int64_t  __s64;
typedef uint64_t __u64;

typedef __u16 __le16;
typedef __u16 __be16;
typedef __u32 __le32;
typedef __u32 __be32;
typedef __u64 __le64;
typedef __u64 __be64;
typedef __u16 __sum16;
typedef __u32 __wsum;

typedef unsigned __poll_t;

typedef unsigned long __kernel_size_t;
typedef long          __kernel_ssize_t;
typedef long          __kernel_long_t;
typedef unsigned long __kernel_ulong_t;
typedef long          __kernel_off_t;
typedef long long     __kernel_loff_t;
typedef int           __kernel_pid_t;
typedef unsigned int  __kernel_uid32_t;
typedef unsigned int  __kernel_gid32_t;
typedef long long     __kernel_time64_t;
typedef __u64 __attribute__((aligned(8))) __aligned_u64;
typedef __s64 __attribute__((aligned(8))) __aligned_s64;

#endif
