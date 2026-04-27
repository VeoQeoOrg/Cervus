#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

void   *memset(void *dst, int c, size_t n);
void   *memcpy(void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
int     memcmp(const void *a, const void *b, size_t n);
void   *memchr(const void *s, int c, size_t n);
void   *memmem(const void *haystack, size_t hlen, const void *needle, size_t nlen);

size_t  strlen(const char *s);
size_t  strnlen(const char *s, size_t max);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
char   *strcat(char *dst, const char *src);
char   *strncat(char *dst, const char *src, size_t n);
char   *strchr(const char *s, int c);
char   *strrchr(const char *s, int c);
char   *strstr(const char *hay, const char *needle);
char   *strdup(const char *s);
size_t  strspn(const char *s, const char *accept);
size_t  strcspn(const char *s, const char *reject);
char   *strtok(char *str, const char *delim);
char   *strtok_r(char *str, const char *delim, char **saveptr);
char   *strerror(int errnum);

#endif