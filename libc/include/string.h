#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>
#include <stdint.h>

void* memcpy (void* restrict dst, const void* restrict src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset (void* dst, int c, size_t n);
int   memcmp (const void* a, const void* b, size_t n);
void *memchr(void *p, int val, size_t n);
void *rawmemchr(void *p, int c);

void* memset_explicit(void* dst, int c, size_t n);

size_t strlen (const char* s);
size_t strnlen(const char* s, size_t maxlen);

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);

char *strcat(char *dest, const char *src);
char* strncat(char* restrict dst, const char* restrict src, size_t n);

int strcmp (const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);

char* strchr (const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr (const char* haystack, const char* needle);
char* strpbrk(const char* s, const char* accept);
size_t strspn (const char* s, const char* accept);
size_t strcspn(const char* s, const char* reject);
char *strtok(char *str, const char *delim);

long strtol (const char* restrict s, char** restrict end, int base);
unsigned long strtoul(const char* restrict s, char** restrict end, int base);

char* strdup(const char* s);

static inline void bzero(void* s, size_t n) {
    memset(s, 0, n);
}

#endif