#ifndef _MALLOC_H
#define _MALLOC_H

#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void  *memalign(size_t align, size_t size);
void  *valloc(size_t size);
void  *pvalloc(size_t size);
size_t malloc_usable_size(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
