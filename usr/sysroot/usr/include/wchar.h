#ifndef _WCHAR_H
#define _WCHAR_H

#include <stddef.h>
#include <stdarg.h>

typedef int wint_t;

#define WEOF ((wint_t)-1)

#ifndef NULL
#define NULL ((void *)0)
#endif

size_t   wcslen(const wchar_t *s);
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

#endif
