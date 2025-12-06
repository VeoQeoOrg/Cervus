#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(void *p, int val, size_t n);
void *rawmemchr(void *p, int c);

size_t strlen(const char *str);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t n);
char *strchr(const char *str, int c);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *str1, const char *str2);
char *strpbrk(const char *str1, const char *str2);
char *strtok(char *str, const char *delim);

#endif