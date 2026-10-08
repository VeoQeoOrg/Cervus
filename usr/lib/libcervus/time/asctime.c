#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <libcervus.h>

static const char g_wday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char g_mon[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

char *asctime_r(const struct tm *restrict tm, char *restrict buf)
{
    if (!tm || !buf) { __cervus_set_errno(EINVAL); return NULL; }
    int wday = tm->tm_wday, mon = tm->tm_mon;
    if (wday < 0 || wday > 6 || mon < 0 || mon > 11) { __cervus_set_errno(EOVERFLOW); return NULL; }
    int n = snprintf(buf, 26, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n", g_wday[wday], g_mon[mon],
                     tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, 1900 + tm->tm_year);
    if (n < 0 || n >= 26) { __cervus_set_errno(EOVERFLOW); return NULL; }
    return buf;
}

char *asctime(const struct tm *tm)
{
    static char buf[26];
    return asctime_r(tm, buf);
}
