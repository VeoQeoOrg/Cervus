#ifndef VMALLOC_H
#define VMALLOC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define VMALLOC_MIN_PAGES 256

void  vmalloc_init(void);
void *vmalloc_pages(size_t pages);
void  vfree_pages(void *addr, size_t pages);
bool  vmalloc_owns(const void *addr);

#endif
