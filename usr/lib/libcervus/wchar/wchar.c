#include <wchar.h>
#include <stdlib.h>

size_t wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int wcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (*a > *b) - (*a < *b);
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (*a > *b) - (*a < *b);
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while ((*d++ = *src++)) { }
    return dst;
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src)
{
    wcscpy(dst + wcslen(dst), src);
    return dst;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
    for (; *s; s++) if (*s == c) return (wchar_t *)s;
    return c ? NULL : (wchar_t *)s;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *last = NULL;
    for (; *s; s++) if (*s == c) last = s;
    if (!c) return (wchar_t *)s;
    return (wchar_t *)last;
}

wchar_t *wcsstr(const wchar_t *h, const wchar_t *n)
{
    if (!*n) return (wchar_t *)h;
    for (; *h; h++) {
        const wchar_t *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (wchar_t *)h;
    }
    return NULL;
}

wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return dst;
}

wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n)
{
    if (dst < src) { for (size_t i = 0; i < n; i++) dst[i] = src[i]; }
    else           { for (size_t i = n; i > 0; i--) dst[i-1] = src[i-1]; }
    return dst;
}

wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n)
{
    for (size_t i = 0; i < n; i++) s[i] = c;
    return s;
}

int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return (a[i] > b[i]) - (a[i] < b[i]);
    return 0;
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    for (size_t i = 0; i < n; i++) if (s[i] == c) return (wchar_t *)(s + i);
    return NULL;
}

int mbtowc(wchar_t *pwc, const char *s, size_t n)
{
    if (!s) return 0;
    if (n == 0) return -1;

    unsigned char c = (unsigned char)s[0];
    int len;
    wchar_t wc;

    if (c < 0x80)        { len = 1; wc = c; }
    else if ((c & 0xE0) == 0xC0) { len = 2; wc = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; wc = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; wc = c & 0x07; }
    else return -1;

    if ((size_t)len > n) return -1;
    for (int i = 1; i < len; i++) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0) != 0x80) return -1;
        wc = (wc << 6) | (cc & 0x3F);
    }
    if (pwc) *pwc = wc;
    return wc ? len : 0;
}

int wctomb(char *s, wchar_t wc)
{
    if (!s) return 0;
    if (wc < 0x80)      { s[0] = (char)wc; return 1; }
    if (wc < 0x800)     { s[0] = (char)(0xC0 | (wc >> 6));  s[1] = (char)(0x80 | (wc & 0x3F)); return 2; }
    if (wc < 0x10000)   { s[0] = (char)(0xE0 | (wc >> 12)); s[1] = (char)(0x80 | ((wc >> 6) & 0x3F)); s[2] = (char)(0x80 | (wc & 0x3F)); return 3; }
    if (wc < 0x110000)  { s[0] = (char)(0xF0 | (wc >> 18)); s[1] = (char)(0x80 | ((wc >> 12) & 0x3F)); s[2] = (char)(0x80 | ((wc >> 6) & 0x3F)); s[3] = (char)(0x80 | (wc & 0x3F)); return 4; }
    return -1;
}

int mblen(const char *s, size_t n)
{
    return mbtowc(NULL, s, n);
}

size_t mbstowcs(wchar_t *dst, const char *src, size_t n)
{
    size_t produced = 0;
    while (!dst || produced < n) {
        wchar_t wc;
        int len = mbtowc(&wc, src, 4);
        if (len < 0) return (size_t)-1;
        if (dst) dst[produced] = wc;
        produced++;
        if (len == 0) return produced - 1;
        src += len;
    }
    return produced;
}

size_t wcstombs(char *dst, const wchar_t *src, size_t n)
{
    size_t produced = 0;
    char tmp[4];
    while (*src) {
        int len = wctomb(tmp, *src);
        if (len < 0) return (size_t)-1;
        if (dst) {
            if (produced + (size_t)len > n) break;
            for (int i = 0; i < len; i++) dst[produced + i] = tmp[i];
        }
        produced += (size_t)len;
        src++;
    }
    if (dst && produced < n) dst[produced] = '\0';
    return produced;
}

wint_t btowc(int c)
{
    if (c < 0 || c > 0x7f) return WEOF;
    return (wint_t)c;
}

int wctob(wint_t c)
{
    if (c < 0 || c > 0x7f) return -1;
    return (int)c;
}
