#include <string.h>
#include <stdlib.h>
#include <stddef.h>

char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *p = (char *)malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}
