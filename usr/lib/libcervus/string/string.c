#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    while (n--) *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; }
    return NULL;
}

void *memmem(const void *haystack, size_t hlen, const void *needle, size_t nlen)
{
    if (nlen == 0) return (void *)haystack;
    if (hlen < nlen) return NULL;
    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;
    size_t last = hlen - nlen;
    for (size_t i = 0; i <= last; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, nlen) == 0)
            return (void *)(h + i);
    }
    return NULL;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) { }
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++)) { }
    return r;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *r = d;
    while (*d) d++;
    for (size_t i = 0; i < n && s[i]; i++) *d++ = s[i];
    *d = '\0';
    return r;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return c == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (; *s; s++) if (*s == (char)c) r = s;
    return (char *)(c == 0 ? s : r);
}

char *strstr(const char *h, const char *n)
{
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    while (s[n]) {
        int found = 0;
        for (size_t i = 0; accept[i]; i++)
            if (s[n] == accept[i]) { found = 1; break; }
        if (!found) break;
        n++;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n]) {
        for (size_t i = 0; reject[i]; i++)
            if (s[n] == reject[i]) return n;
        n++;
    }
    return n;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}

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

static char *__strtok_save = NULL;
char *strtok(char *str, const char *delim)
{
    return strtok_r(str, delim, &__strtok_save);
}

char *strerror(int err)
{
    switch (err) {
        case 0:       return "Success";
        case EPERM:   return "Operation not permitted";
        case ENOENT:  return "No such file or directory";
        case ESRCH:   return "No such process";
        case EINTR:   return "Interrupted system call";
        case EIO:     return "Input/output error";
        case EBADF:   return "Bad file descriptor";
        case ECHILD:  return "No child processes";
        case EAGAIN:  return "Resource temporarily unavailable";
        case ENOMEM:  return "Cannot allocate memory";
        case EACCES:  return "Permission denied";
        case EFAULT:  return "Bad address";
        case EBUSY:   return "Device or resource busy";
        case EEXIST:  return "File exists";
        case ENODEV:  return "No such device";
        case ENOTDIR: return "Not a directory";
        case EISDIR:  return "Is a directory";
        case EINVAL:  return "Invalid argument";
        case EMFILE:  return "Too many open files";
        case ENOTTY:  return "Inappropriate ioctl for device";
        case ENOSPC:  return "No space left on device";
        case EPIPE:   return "Broken pipe";
        case ENOSYS:  return "Function not implemented";
        default:      return "Unknown error";
    }
}