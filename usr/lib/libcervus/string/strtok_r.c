#include <string.h>
#include <stddef.h>

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *s = str ? str : (saveptr ? *saveptr : NULL);
    if (!s || !delim) return NULL;

    while (*s) {
        const char *d;
        for (d = delim; *d; d++) if (*s == *d) break;
        if (!*d) break;
        s++;
    }
    if (!*s) {
        if (saveptr) *saveptr = NULL;
        return NULL;
    }
    char *tok = s;

    while (*s) {
        const char *d;
        for (d = delim; *d; d++) if (*s == *d) break;
        if (*d) {
            *s = '\0';
            if (saveptr) *saveptr = s + 1;
            return tok;
        }
        s++;
    }
    if (saveptr) *saveptr = NULL;
    return tok;
}
