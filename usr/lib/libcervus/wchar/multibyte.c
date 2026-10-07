#include <wchar.h>
#include <errno.h>
#include <libcervus.h>

static mbstate_t g_mbrtowc_state;
static mbstate_t g_mbrlen_state;
static mbstate_t g_wcrtomb_state;
static mbstate_t g_mbsrtowcs_state;
static mbstate_t g_wcsrtombs_state;

int mbsinit(const mbstate_t *ps)
{
    return !ps || ps->__count == 0;
}

static size_t utf8_min(unsigned len)
{
    return len == 2 ? 0x80 : len == 3 ? 0x800 : 0x10000;
}

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps)
{
    if (!ps) ps = &g_mbrtowc_state;
    if (!s) {
        if (ps->__count) { ps->__count = 0; __cervus_set_errno(EILSEQ); return (size_t)-1; }
        return 0;
    }
    if (n == 0) return (size_t)-2;

    const unsigned char *p = (const unsigned char *)s;
    unsigned need = ps->__count & 0xFF;
    unsigned total = ps->__count >> 8;
    unsigned val = ps->__value;
    size_t i = 0;

    if (!need) {
        unsigned c = p[0];
        if (c < 0x80) {
            if (pwc) *pwc = (wchar_t)c;
            return c ? 1 : 0;
        }
        if (c >= 0xC2 && c <= 0xDF)      { need = 1; val = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; val = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; val = c & 0x07; }
        else { __cervus_set_errno(EILSEQ); return (size_t)-1; }
        total = need + 1;
        i = 1;
    }

    while (need && i < n) {
        unsigned c = p[i];
        if ((c & 0xC0) != 0x80) {
            ps->__count = 0;
            __cervus_set_errno(EILSEQ);
            return (size_t)-1;
        }
        val = (val << 6) | (c & 0x3F);
        need--;
        i++;
    }

    if (need) {
        ps->__count = (total << 8) | need;
        ps->__value = val;
        return (size_t)-2;
    }

    ps->__count = 0;
    ps->__value = 0;
    if (val < utf8_min(total) || val > 0x10FFFF || (val >= 0xD800 && val <= 0xDFFF)) {
        __cervus_set_errno(EILSEQ);
        return (size_t)-1;
    }
    if (pwc) *pwc = (wchar_t)val;
    return val ? i : 0;
}

size_t mbrlen(const char *s, size_t n, mbstate_t *ps)
{
    return mbrtowc(NULL, s, n, ps ? ps : &g_mbrlen_state);
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
    if (!ps) ps = &g_wcrtomb_state;
    if (!s) { ps->__count = 0; return 1; }
    unsigned c = (unsigned)wc;
    if (c < 0x80) { s[0] = (char)c; return 1; }
    if (c < 0x800) {
        s[0] = (char)(0xC0 | (c >> 6));
        s[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c >= 0xD800 && c <= 0xDFFF) { __cervus_set_errno(EILSEQ); return (size_t)-1; }
    if (c < 0x10000) {
        s[0] = (char)(0xE0 | (c >> 12));
        s[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        s[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    if (c < 0x110000) {
        s[0] = (char)(0xF0 | (c >> 18));
        s[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        s[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        s[3] = (char)(0x80 | (c & 0x3F));
        return 4;
    }
    __cervus_set_errno(EILSEQ);
    return (size_t)-1;
}

size_t mbsnrtowcs(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps)
{
    if (!ps) ps = &g_mbsrtowcs_state;
    const char *s = *src;
    size_t count = 0;
    while (!dst || count < len) {
        if (nms == 0) break;
        wchar_t wc;
        size_t r = mbrtowc(&wc, s, nms, ps);
        if (r == (size_t)-1) { if (dst) *src = s; return (size_t)-1; }
        if (r == (size_t)-2) { s += nms; nms = 0; break; }
        if (r == 0) {
            if (dst) { dst[count] = 0; *src = NULL; }
            return count;
        }
        if (dst) dst[count] = wc;
        count++;
        s += r;
        nms -= r;
    }
    if (dst) *src = s;
    return count;
}

size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps)
{
    return mbsnrtowcs(dst, src, (size_t)-1, len, ps);
}

size_t wcsnrtombs(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps)
{
    if (!ps) ps = &g_wcsrtombs_state;
    const wchar_t *w = *src;
    size_t count = 0;
    char buf[4];
    while (nwc) {
        if (*w == 0) {
            if (dst) {
                if (count >= len) break;
                dst[count] = 0;
                *src = NULL;
            }
            return count;
        }
        size_t r = wcrtomb(buf, *w, ps);
        if (r == (size_t)-1) { if (dst) *src = w; return (size_t)-1; }
        if (dst) {
            if (count + r > len) break;
            for (size_t i = 0; i < r; i++) dst[count + i] = buf[i];
        }
        count += r;
        w++;
        nwc--;
    }
    if (dst) *src = w;
    return count;
}

size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps)
{
    return wcsnrtombs(dst, src, (size_t)-1, len, ps);
}
