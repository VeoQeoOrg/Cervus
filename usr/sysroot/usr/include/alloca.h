#ifndef _ALLOCA_H
#define _ALLOCA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define alloca(size) __builtin_alloca(size)

#ifdef __cplusplus
}
#endif
#endif
