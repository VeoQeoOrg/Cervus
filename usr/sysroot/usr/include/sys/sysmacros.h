#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline unsigned int gnu_dev_major(dev_t dev)
{
    return (unsigned int)(((dev >> 32) & 0xfffff000u) | ((dev >> 8) & 0xfffu));
}

static inline unsigned int gnu_dev_minor(dev_t dev)
{
    return (unsigned int)(((dev >> 12) & 0xffffff00u) | (dev & 0xffu));
}

static inline dev_t gnu_dev_makedev(unsigned int major, unsigned int minor)
{
    return ((dev_t)(major & 0xfffff000u) << 32) | ((dev_t)(major & 0xfffu) << 8) |
           ((dev_t)(minor & 0xffffff00u) << 12) | (dev_t)(minor & 0xffu);
}

#define major(dev)        gnu_dev_major(dev)
#define minor(dev)        gnu_dev_minor(dev)
#define makedev(maj, min) gnu_dev_makedev((maj), (min))

#ifdef __cplusplus
}
#endif

#endif
