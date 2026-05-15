#include <string.h>
#include <stddef.h>

char *strchr(const char *s, int c)
{
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return c == 0 ? (char *)s : NULL;
}
