#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define RAND_MAX 0x7FFFFFFF

void *malloc(size_t n);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *p, size_t n);
void  free(void *p);

void  exit(int status) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));
int   atexit(void (*fn)(void));

int      atoi(const char *s);
long     atol(const char *s);
long long atoll(const char *s);
long     strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);

#include <stdint.h>
uint64_t __cervus_strtod_bits(const char *s, char **endptr);

static __inline__ double strtod(const char *s, char **endp)
{
    uint64_t b = __cervus_strtod_bits(s, endp);
    double d;
    __builtin_memcpy(&d, &b, sizeof(d));
    return d;
}

static __inline__ float strtof(const char *s, char **endp)
{
    return (float)strtod(s, endp);
}

static __inline__ long double strtold(const char *s, char **endp)
{
    return (long double)strtod(s, endp);
}

static __inline__ double atof(const char *s)
{
    return strtod(s, (char **)0);
}

int      abs(int x);
long     labs(long x);
long long llabs(long long x);

int      rand(void);
void     srand(unsigned int seed);

char    *getenv(const char *name);

void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *));

int system(const char *cmd);

int mkstemp(char *template);

#endif