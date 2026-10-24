#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <time.h>
#include <langinfo.h>
#include <locale.h>
#include <inttypes.h>
#include "darwin.h"

#define L void *loc

static int d_snprintf_l(char *s, size_t n, L, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

static int d_vsnprintf_l(char *s, size_t n, L, const char *fmt, va_list ap) { (void)loc; return vsnprintf(s, n, fmt, ap); }

static int d_sprintf_l(char *s, L, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}

static int d_asprintf_l(char **s, L, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(s, fmt, ap);
    va_end(ap);
    return r;
}

static int d_vasprintf_l(char **s, L, const char *fmt, va_list ap) { (void)loc; return vasprintf(s, fmt, ap); }

static int d_sscanf_l(const char *s, L, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

static int d_vsscanf_l(const char *s, L, const char *fmt, va_list ap) { (void)loc; return vsscanf(s, fmt, ap); }

static int d_fprintf_l(void *f, L, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(d_real_file(f), fmt, ap);
    va_end(ap);
    return r;
}

static int d_printf_l(L, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

DSYM("_snprintf_l", d_snprintf_l);
DSYM("_vsnprintf_l", d_vsnprintf_l);
DSYM("_sprintf_l", d_sprintf_l);
DSYM("_asprintf_l", d_asprintf_l);
DSYM("_vasprintf_l", d_vasprintf_l);
DSYM("_sscanf_l", d_sscanf_l);
DSYM("_vsscanf_l", d_vsscanf_l);
DSYM("_fprintf_l", d_fprintf_l);
DSYM("_printf_l", d_printf_l);

static long long d_strtoll_l(const char *s, char **e, int b, L) { (void)loc; return strtoll(s, e, b); }
static unsigned long long d_strtoull_l(const char *s, char **e, int b, L) { (void)loc; return strtoull(s, e, b); }
static long d_strtol_l(const char *s, char **e, int b, L) { (void)loc; return strtol(s, e, b); }
static unsigned long d_strtoul_l(const char *s, char **e, int b, L) { (void)loc; return strtoul(s, e, b); }
static intmax_t d_strtoimax_l(const char *s, char **e, int b, L) { (void)loc; return strtoimax(s, e, b); }
static uintmax_t d_strtoumax_l(const char *s, char **e, int b, L) { (void)loc; return strtoumax(s, e, b); }
static double d_strtod_l(const char *s, char **e, L) { (void)loc; return strtod(s, e); }
static float d_strtof_l(const char *s, char **e, L) { (void)loc; return strtof(s, e); }
static long double d_strtold_l(const char *s, char **e, L) { (void)loc; return strtold(s, e); }
static double d_atof_l(const char *s, L) { (void)loc; return atof(s); }
static int d_atoi_l(const char *s, L) { (void)loc; return atoi(s); }

DSYM("_strtoll_l", d_strtoll_l);
DSYM("_strtoull_l", d_strtoull_l);
DSYM("_strtol_l", d_strtol_l);
DSYM("_strtoul_l", d_strtoul_l);
DSYM("_strtoimax_l", d_strtoimax_l);
DSYM("_strtoumax_l", d_strtoumax_l);
DSYM("_strtod_l", d_strtod_l);
DSYM("_strtof_l", d_strtof_l);
DSYM("_strtold_l", d_strtold_l);
DSYM("_atof_l", d_atof_l);
DSYM("_atoi_l", d_atoi_l);

static int d_strcoll_l(const char *a, const char *b, L) { (void)loc; return strcoll(a, b); }
static size_t d_strxfrm_l(char *d, const char *s, size_t n, L) { (void)loc; return strxfrm(d, s, n); }
static int d_wcscoll_l(const wchar_t *a, const wchar_t *b, L) { (void)loc; return wcscoll(a, b); }
static size_t d_wcsxfrm_l(wchar_t *d, const wchar_t *s, size_t n, L) { (void)loc; return wcsxfrm(d, s, n); }
static int d_strcasecmp_l(const char *a, const char *b, L) { (void)loc; return strcasecmp(a, b); }
static int d_strncasecmp_l(const char *a, const char *b, size_t n, L) { (void)loc; return strncasecmp(a, b, n); }

static size_t d_strftime_l(char *s, size_t n, const char *f, const int *tm, L)
{
    (void)loc;
    struct tm t;
    memcpy(&t, tm, sizeof t);
    return strftime(s, n, f, &t);
}

static char *d_strptime_l(const char *s, const char *f, int *tm, L)
{
    (void)loc;
    struct tm t;
    memcpy(&t, tm, sizeof t);
    char *r = strptime(s, f, &t);
    memcpy(tm, &t, sizeof t);
    return r;
}

DSYM("_strcoll_l", d_strcoll_l);
DSYM("_strxfrm_l", d_strxfrm_l);
DSYM("_wcscoll_l", d_wcscoll_l);
DSYM("_wcsxfrm_l", d_wcsxfrm_l);
DSYM("_strcasecmp_l", d_strcasecmp_l);
DSYM("_strncasecmp_l", d_strncasecmp_l);
DSYM("_strftime_l", d_strftime_l);
DSYM("_strptime_l", d_strptime_l);

static wint_t d_btowc_l(int c, L) { (void)loc; return btowc(c); }
static int d_wctob_l(wint_t c, L) { (void)loc; return wctob(c); }
static size_t d_mbrtowc_l(wchar_t *w, const char *s, size_t n, mbstate_t *st, L) { (void)loc; return mbrtowc(w, s, n, st); }
static size_t d_wcrtomb_l(char *s, wchar_t w, mbstate_t *st, L) { (void)loc; return wcrtomb(s, w, st); }
static size_t d_mbrlen_l(const char *s, size_t n, mbstate_t *st, L) { (void)loc; return mbrlen(s, n, st); }
static int d_mbtowc_l(wchar_t *w, const char *s, size_t n, L) { (void)loc; return mbtowc(w, s, n); }
static int d_wctomb_l(char *s, wchar_t w, L) { (void)loc; return wctomb(s, w); }
static size_t d_mbstowcs_l(wchar_t *d, const char *s, size_t n, L) { (void)loc; return mbstowcs(d, s, n); }
static size_t d_wcstombs_l(char *d, const wchar_t *s, size_t n, L) { (void)loc; return wcstombs(d, s, n); }
static size_t d_mbsrtowcs_l(wchar_t *d, const char **s, size_t n, mbstate_t *st, L) { (void)loc; return mbsrtowcs(d, s, n, st); }
static size_t d_wcsrtombs_l(char *d, const wchar_t **s, size_t n, mbstate_t *st, L) { (void)loc; return wcsrtombs(d, s, n, st); }

static size_t d_mbsnrtowcs_l(wchar_t *d, const char **src, size_t nms, size_t len, mbstate_t *st, L)
{
    (void)loc;
    const char *s = *src;
    size_t done = 0, used = 0;
    while (used < nms && (!d || done < len)) {
        wchar_t w;
        size_t k = mbrtowc(&w, s + used, nms - used, st);
        if (k == (size_t)-1)
            return (size_t)-1;
        if (k == (size_t)-2)
            break;
        if (d)
            d[done] = w;
        if (!k) {
            if (d)
                *src = NULL;
            return done;
        }
        used += k;
        done++;
    }
    if (d)
        *src = s + used;
    return done;
}

static size_t d_wcsnrtombs_l(char *d, const wchar_t **src, size_t nwc, size_t len, mbstate_t *st, L)
{
    (void)loc;
    const wchar_t *s = *src;
    size_t done = 0, i = 0;
    char buf[8];
    while (i < nwc) {
        size_t k = wcrtomb(buf, s[i], st);
        if (k == (size_t)-1)
            return (size_t)-1;
        if (d) {
            if (done + k > len)
                break;
            memcpy(d + done, buf, k);
        }
        if (!s[i]) {
            if (d)
                *src = NULL;
            return done;
        }
        done += k;
        i++;
    }
    if (d)
        *src = s + i;
    return done;
}

DSYM("_btowc_l", d_btowc_l);
DSYM("_wctob_l", d_wctob_l);
DSYM("_mbrtowc_l", d_mbrtowc_l);
DSYM("_wcrtomb_l", d_wcrtomb_l);
DSYM("_mbrlen_l", d_mbrlen_l);
DSYM("_mbtowc_l", d_mbtowc_l);
DSYM("_wctomb_l", d_wctomb_l);
DSYM("_mbstowcs_l", d_mbstowcs_l);
DSYM("_wcstombs_l", d_wcstombs_l);
DSYM("_mbsrtowcs_l", d_mbsrtowcs_l);
DSYM("_wcsrtombs_l", d_wcsrtombs_l);
DSYM("_mbsnrtowcs_l", d_mbsnrtowcs_l);
DSYM("_wcsnrtombs_l", d_wcsnrtombs_l);
DSYM("_mbsnrtowcs", d_mbsnrtowcs_l);
DSYM("_wcsnrtombs", d_wcsnrtombs_l);

static int d_isalnum_l(int c, L) { (void)loc; return isalnum(c); }
static int d_isalpha_l(int c, L) { (void)loc; return isalpha(c); }
static int d_isblank_l(int c, L) { (void)loc; return isblank(c); }
static int d_iscntrl_l(int c, L) { (void)loc; return iscntrl(c); }
static int d_isdigit_l(int c, L) { (void)loc; return isdigit(c); }
static int d_isgraph_l(int c, L) { (void)loc; return isgraph(c); }
static int d_islower_l(int c, L) { (void)loc; return islower(c); }
static int d_isprint_l(int c, L) { (void)loc; return isprint(c); }
static int d_ispunct_l(int c, L) { (void)loc; return ispunct(c); }
static int d_isspace_l(int c, L) { (void)loc; return isspace(c); }
static int d_isupper_l(int c, L) { (void)loc; return isupper(c); }
static int d_isxdigit_l(int c, L) { (void)loc; return isxdigit(c); }
static int d_toupper_l(int c, L) { (void)loc; return toupper(c); }
static int d_tolower_l(int c, L) { (void)loc; return tolower(c); }
static int d_iswalnum_l(wint_t c, L) { (void)loc; return iswalnum(c); }
static int d_iswalpha_l(wint_t c, L) { (void)loc; return iswalpha(c); }
static int d_iswblank_l(wint_t c, L) { (void)loc; return iswblank(c); }
static int d_iswcntrl_l(wint_t c, L) { (void)loc; return iswcntrl(c); }
static int d_iswdigit_l(wint_t c, L) { (void)loc; return iswdigit(c); }
static int d_iswgraph_l(wint_t c, L) { (void)loc; return iswgraph(c); }
static int d_iswlower_l(wint_t c, L) { (void)loc; return iswlower(c); }
static int d_iswprint_l(wint_t c, L) { (void)loc; return iswprint(c); }
static int d_iswpunct_l(wint_t c, L) { (void)loc; return iswpunct(c); }
static int d_iswspace_l(wint_t c, L) { (void)loc; return iswspace(c); }
static int d_iswupper_l(wint_t c, L) { (void)loc; return iswupper(c); }
static int d_iswxdigit_l(wint_t c, L) { (void)loc; return iswxdigit(c); }
static int d_iswctype_l(wint_t c, wctype_t t, L) { (void)loc; return iswctype(c, t); }
static wctype_t d_wctype_l(const char *n, L) { (void)loc; return wctype(n); }
static wint_t d_towupper_l(wint_t c, L) { (void)loc; return towupper(c); }
static wint_t d_towlower_l(wint_t c, L) { (void)loc; return towlower(c); }
static int d_wcwidth_l(wchar_t c, L) { (void)loc; return wcwidth(c); }
static char *d_nl_langinfo_l(int item, L)
{
    (void)loc;
    char *(*f)(int) = (char *(*)(int))dsym_lookup("_nl_langinfo");
    return f ? f(item) : "";
}
static void *d_localeconv_l(L)
{
    (void)loc;
    void *(*f)(void) = (void *(*)(void))dsym_lookup("_localeconv");
    return f ? f() : NULL;
}

DSYM("_isalnum_l", d_isalnum_l);
DSYM("_isalpha_l", d_isalpha_l);
DSYM("_isblank_l", d_isblank_l);
DSYM("_iscntrl_l", d_iscntrl_l);
DSYM("_isdigit_l", d_isdigit_l);
DSYM("_isgraph_l", d_isgraph_l);
DSYM("_islower_l", d_islower_l);
DSYM("_isprint_l", d_isprint_l);
DSYM("_ispunct_l", d_ispunct_l);
DSYM("_isspace_l", d_isspace_l);
DSYM("_isupper_l", d_isupper_l);
DSYM("_isxdigit_l", d_isxdigit_l);
DSYM("_toupper_l", d_toupper_l);
DSYM("_tolower_l", d_tolower_l);
DSYM("_iswalnum_l", d_iswalnum_l);
DSYM("_iswalpha_l", d_iswalpha_l);
DSYM("_iswblank_l", d_iswblank_l);
DSYM("_iswcntrl_l", d_iswcntrl_l);
DSYM("_iswdigit_l", d_iswdigit_l);
DSYM("_iswgraph_l", d_iswgraph_l);
DSYM("_iswlower_l", d_iswlower_l);
DSYM("_iswprint_l", d_iswprint_l);
DSYM("_iswpunct_l", d_iswpunct_l);
DSYM("_iswspace_l", d_iswspace_l);
DSYM("_iswupper_l", d_iswupper_l);
DSYM("_iswxdigit_l", d_iswxdigit_l);
DSYM("_iswctype_l", d_iswctype_l);
DSYM("_wctype_l", d_wctype_l);
DSYM("_towupper_l", d_towupper_l);
DSYM("_towlower_l", d_towlower_l);
DSYM("_wcwidth_l", d_wcwidth_l);
DSYM("_nl_langinfo_l", d_nl_langinfo_l);
DSYM("_localeconv_l", d_localeconv_l);
