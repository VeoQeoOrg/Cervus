#ifndef _SYS_EVENTFD_H
#define _SYS_EVENTFD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define EFD_SEMAPHORE 1
#define EFD_CLOEXEC  0x80000
#define EFD_NONBLOCK  0x800

typedef uint64_t eventfd_t;

int eventfd(unsigned int initval, int flags);
int eventfd_read(int fd, eventfd_t *value);
int eventfd_write(int fd, eventfd_t value);

#ifdef __cplusplus
}
#endif
#endif
