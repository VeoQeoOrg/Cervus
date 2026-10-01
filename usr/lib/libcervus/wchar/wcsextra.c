#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <string.h>

size_t wcsnlen(const wchar_t *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst + wcslen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = 0;
    return dst;
}

wchar_t *wcsdup(const wchar_t *s)
{
    size_t n = wcslen(s) + 1;
    wchar_t *d = malloc(n * sizeof(wchar_t));
    if (d) wmemcpy(d, s, n);
    return d;
}

size_t wcsspn(const wchar_t *s, const wchar_t *accept)
{
    size_t n = 0;
    while (s[n] && wcschr(accept, s[n])) n++;
    return n;
}

size_t wcscspn(const wchar_t *s, const wchar_t *reject)
{
    size_t n = 0;
    while (s[n] && !wcschr(reject, s[n])) n++;
    return n;
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept)
{
    s += wcscspn(s, accept);
    return *s ? (wchar_t *)s : NULL;
}

wchar_t *wcstok(wchar_t *s, const wchar_t *delim, wchar_t **save)
{
    if (!s) s = *save;
    if (!s) return NULL;
    s += wcsspn(s, delim);
    if (!*s) { *save = NULL; return NULL; }
    wchar_t *end = s + wcscspn(s, delim);
    if (*end) { *end = 0; *save = end + 1; }
    else *save = NULL;
    return s;
}

wchar_t *wcpcpy(wchar_t *dst, const wchar_t *src)
{
    while ((*dst = *src++)) dst++;
    return dst;
}

wchar_t *wcpncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    wchar_t *end = dst + i;
    for (; i < n; i++) dst[i] = 0;
    return end;
}

int wcsncasecmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (; n; a++, b++, n--) {
        wint_t x = towlower((wint_t)*a), y = towlower((wint_t)*b);
        if (x != y || !*a) return (x > y) - (x < y);
    }
    return 0;
}

int wcscasecmp(const wchar_t *a, const wchar_t *b)
{
    return wcsncasecmp(a, b, (size_t)-1);
}

int wcscoll(const wchar_t *a, const wchar_t *b)
{
    return wcscmp(a, b);
}

size_t wcsxfrm(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t len = wcslen(src);
    if (n) {
        size_t k = len < n - 1 ? len : n - 1;
        wmemcpy(dst, src, k);
        dst[k] = 0;
    }
    return len;
}

static const wchar_t *narrow(const wchar_t *s, char *buf, size_t cap)
{
    while (iswspace((wint_t)*s)) s++;
    size_t i = 0;
    while (i + 1 < cap && s[i] > 0 && s[i] < 0x80) { buf[i] = (char)s[i]; i++; }
    buf[i] = 0;
    return s;
}

#define WIDE_TO_NUM(type, name, conv, ...)                      \
    type name(const wchar_t *s, wchar_t **end __VA_ARGS__)      \
    {                                                           \
        char buf[512], *e;                                      \
        const wchar_t *start = narrow(s, buf, sizeof buf);      \
        type v = conv;                                          \
        if (end) *end = (wchar_t *)(e == buf ? s : start + (e - buf)); \
        return v;                                               \
    }

WIDE_TO_NUM(long, wcstol, strtol(buf, &e, base), , int base)
WIDE_TO_NUM(unsigned long, wcstoul, strtoul(buf, &e, base), , int base)
WIDE_TO_NUM(long long, wcstoll, strtoll(buf, &e, base), , int base)
WIDE_TO_NUM(unsigned long long, wcstoull, strtoull(buf, &e, base), , int base)
WIDE_TO_NUM(double, wcstod, strtod(buf, &e))
WIDE_TO_NUM(float, wcstof, strtof(buf, &e))
WIDE_TO_NUM(long double, wcstold, strtold(buf, &e))
