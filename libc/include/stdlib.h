#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <math.h>

char* itoa(int val, char* restrict str, int base);

long          strtol (const char* restrict s, char** restrict end, int base);
unsigned long strtoul(const char* restrict s, char** restrict end, int base);

static inline int  atoi(const char* s) { return (int)strtol(s, (char**)0, 10); }
static inline long atol(const char* s) {      return strtol(s, (char**)0, 10); }

void  malloc_init(void);
void* malloc (size_t size);
void* calloc (size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void  free (void* ptr);
void* aligned_alloc(size_t alignment, size_t size);
void  aligned_free (void* ptr);

static inline void abort(void) {
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

#endif