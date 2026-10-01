#ifndef _WCHAR_H
#define _WCHAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdarg.h>

typedef int wint_t;

typedef struct {
    unsigned __count;
    unsigned __value;
} mbstate_t;

#define WEOF ((wint_t)-1)
#ifndef WCHAR_MIN
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647
#endif


#ifndef NULL
#define NULL ((void *)0)
#endif

size_t   wcslen(const wchar_t *s);
int      wcwidth(wchar_t c);
int      wcswidth(const wchar_t *s, size_t n);
int      wcscmp(const wchar_t *a, const wchar_t *b);
int      wcsncmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src);
wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wcscat(wchar_t *dst, const wchar_t *src);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *h, const wchar_t *n);
wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
int      wmemcmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);

size_t   mbstowcs(wchar_t *dst, const char *src, size_t n);
size_t   wcstombs(char *dst, const wchar_t *src, size_t n);
int      mbtowc(wchar_t *pwc, const char *s, size_t n);
int      wctomb(char *s, wchar_t wc);
int      mblen(const char *s, size_t n);
wint_t   btowc(int c);
int      wctob(wint_t c);

int      mbsinit(const mbstate_t *ps);
size_t   mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t   mbrlen(const char *s, size_t n, mbstate_t *ps);
size_t   wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t   mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t   mbsnrtowcs(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps);
size_t   wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);
size_t   wcsnrtombs(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps);

size_t   wcsnlen(const wchar_t *s, size_t max);
wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wcsdup(const wchar_t *s);
size_t   wcsspn(const wchar_t *s, const wchar_t *accept);
size_t   wcscspn(const wchar_t *s, const wchar_t *reject);
wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept);
wchar_t *wcstok(wchar_t *s, const wchar_t *delim, wchar_t **save);
wchar_t *wcpcpy(wchar_t *dst, const wchar_t *src);
wchar_t *wcpncpy(wchar_t *dst, const wchar_t *src, size_t n);
int      wcscasecmp(const wchar_t *a, const wchar_t *b);
int      wcsncasecmp(const wchar_t *a, const wchar_t *b, size_t n);
int      wcscoll(const wchar_t *a, const wchar_t *b);
size_t   wcsxfrm(wchar_t *dst, const wchar_t *src, size_t n);
long               wcstol(const wchar_t *s, wchar_t **end, int base);
unsigned long      wcstoul(const wchar_t *s, wchar_t **end, int base);
long long          wcstoll(const wchar_t *s, wchar_t **end, int base);
unsigned long long wcstoull(const wchar_t *s, wchar_t **end, int base);
double             wcstod(const wchar_t *s, wchar_t **end);
float              wcstof(const wchar_t *s, wchar_t **end);
long double        wcstold(const wchar_t *s, wchar_t **end);

#ifdef __cplusplus
}
#endif
#endif
