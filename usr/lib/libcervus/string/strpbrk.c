#include <string.h>
#include <stddef.h>

char *strpbrk(const char *s, const char *accept)
{
    while (*s) {
        for (const char *a = accept; *a; a++)
            if (*s == *a) return (char *)s;
        s++;
    }
    return NULL;
}
