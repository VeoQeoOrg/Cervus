#include <string.h>

char *strncat(char *d, const char *s, size_t n)
{
    char *r = d;
    while (*d) d++;
    for (size_t i = 0; i < n && s[i]; i++) *d++ = s[i];
    *d = '\0';
    return r;
}
