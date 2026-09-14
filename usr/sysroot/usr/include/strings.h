#ifndef _STRINGS_H
#define _STRINGS_H

#include <stddef.h>

int  strcasecmp(const char *a, const char *b);
int  strncasecmp(const char *a, const char *b, size_t n);
char *index(const char *s, int c);
char *rindex(const char *s, int c);
int  ffs(int i);

#endif
