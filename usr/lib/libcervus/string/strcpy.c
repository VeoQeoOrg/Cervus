#include <string.h>

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) { }
    return r;
}
