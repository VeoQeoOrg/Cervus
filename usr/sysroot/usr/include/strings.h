#ifndef _STRINGS_H
#define _STRINGS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

int  strcasecmp(const char *a, const char *b);
int  strncasecmp(const char *a, const char *b, size_t n);
char *index(const char *s, int c);
char *rindex(const char *s, int c);
int  ffs(int i);
int  ffsl(long i);
int  ffsll(long long i);
void bzero(void *s, size_t n);
void bcopy(const void *src, void *dest, size_t n);
int  bcmp(const void *a, const void *b, size_t n);

#ifdef __cplusplus
}
#endif
#endif
