#include <string.h>
#include <stddef.h>

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (; *s; s++) if (*s == (char)c) r = s;
    return (char *)(c == 0 ? s : r);
}
