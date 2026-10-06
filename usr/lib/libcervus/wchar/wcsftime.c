#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

size_t wcsftime(wchar_t *s, size_t max, const wchar_t *fmt, const struct tm *tm)
{
    if (!s || !max || !fmt) return 0;
    char mfmt[1024], out[4096];
    size_t n = wcstombs(mfmt, fmt, sizeof mfmt);
    if (n == (size_t)-1 || n >= sizeof mfmt) return 0;
    size_t m = strftime(out, sizeof out, mfmt, tm);
    if (m == 0 && mfmt[0]) return 0;
    size_t w = mbstowcs(s, out, max);
    if (w == (size_t)-1 || w >= max) return 0;
    return w;
}
