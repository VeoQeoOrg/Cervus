#include <string.h>
#include <stdlib.h>
#include <stddef.h>

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}
