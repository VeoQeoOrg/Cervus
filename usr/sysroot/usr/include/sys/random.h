#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GRND_NONBLOCK 0x01
#define GRND_RANDOM   0x02
#define GRND_INSECURE 0x04

ssize_t getrandom(void *buf, size_t len, unsigned int flags);
int     getentropy(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
