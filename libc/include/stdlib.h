#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <math.h>

char *itoa(int val, char *restrict str, int base);

void malloc_init(void);

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *aligned_alloc(size_t alignment, size_t size);


#endif
