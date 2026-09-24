#ifndef _STDLIB_H
#define _STDLIB_H
#ifdef __cplusplus
extern "C" {
#endif


typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

div_t   div(int num, int den);
ldiv_t  ldiv(long num, long den);
lldiv_t lldiv(long long num, long long den);

#include <stddef.h>
#include <alloca.h>

#define MB_CUR_MAX 1

int  __cxa_atexit(void (*fn)(void *), void *arg, void *dso);
void __cxa_finalize(void *dso);

int    mblen(const char *s, size_t n);
int    mbtowc(wchar_t *pwc, const char *s, size_t n);
int    wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *dst, const char *src, size_t n);
size_t wcstombs(char *dst, const wchar_t *src, size_t n);


#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define RAND_MAX 0x7FFFFFFF

void *malloc(size_t n);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *p, size_t n);
int   posix_memalign(void **out, size_t align, size_t n);
void *aligned_alloc(size_t align, size_t n);
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

#ifndef __CERVUS_LOCALE_T
#define __CERVUS_LOCALE_T
typedef struct __cervus_locale *locale_t;
#endif
double      strtod_l(const char *s, char **end, locale_t loc);
float       strtof_l(const char *s, char **end, locale_t loc);
long double strtold_l(const char *s, char **end, locale_t loc);

int      abs(int x);
long     labs(long x);
long long llabs(long long x);

int      rand(void);
void     srand(unsigned int seed);
long     random(void);
void     srandom(unsigned int seed);
char    *initstate(unsigned int seed, char *state, size_t n);
char    *setstate(char *state);

char    *getenv(const char *name);
int      putenv(char *str);
int      setenv(const char *name, const char *value, int overwrite);
int      unsetenv(const char *name);

void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *));

int system(const char *cmd);

int    mkstemp(char *pattern);
char  *mktemp(char *pattern);
char  *mkdtemp(char *pattern);
char  *realpath(const char *path, char *resolved);

#ifdef __cplusplus
}
#endif
#endif