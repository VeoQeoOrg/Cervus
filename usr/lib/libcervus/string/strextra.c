#include <string.h>
#include <ctype.h>

char *strcasestr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return NULL;
}

char *strchrnul(const char *s, int c) {
    while (*s && *s != (char)c) s++;
    return (char *)s;
}

char *stpcpy(char *d, const char *s) {
    while ((*d = *s)) { d++; s++; }
    return d;
}

char *stpncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    char *end = d + i;
    for (; i < n; i++) d[i] = '\0';
    return end;
}

void *memccpy(void *dst, const void *src, int c, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) {
        *d = *s;
        if (*s == (unsigned char)c) return d + 1;
        d++; s++;
    }
    return NULL;
}
