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

#ifdef __cplusplus
}
#endif
#endif
