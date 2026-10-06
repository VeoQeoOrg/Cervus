#include <time.h>
#include <errno.h>
#include <limits.h>

extern long long __cervus_days_from_civil(long long y, int m, int d);
extern void __cervus_civil_from_days(long long z, long long *y, int *m, int *d);

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    if (!t || !out) { errno = EINVAL; return 0; }
    long long sec = (long long)*t;
    long long days = sec / 86400, rem = sec % 86400;
    if (rem < 0) { rem += 86400; days--; }
    long long y;
    int m, d;
    __cervus_civil_from_days(days, &y, &m, &d);
    if (y - 1900 > INT_MAX || y - 1900 < INT_MIN) { errno = EOVERFLOW; return 0; }
    out->tm_year = (int)(y - 1900);
    out->tm_mon = m - 1;
    out->tm_mday = d;
    out->tm_hour = (int)(rem / 3600);
    out->tm_min = (int)(rem / 60 % 60);
    out->tm_sec = (int)(rem % 60);
    long long wd = (days + 4) % 7;
    out->tm_wday = (int)(wd < 0 ? wd + 7 : wd);
    out->tm_yday = (int)(days - __cervus_days_from_civil(y, 1, 1));
    out->tm_isdst = 0;
    return out;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm buf;
    return gmtime_r(t, &buf);
}
