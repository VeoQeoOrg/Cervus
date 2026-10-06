#include <time.h>
#include <errno.h>

extern long long __cervus_days_from_civil(long long y, int m, int d);
extern long timezone_offset(void);

static long long fields_to_seconds(const struct tm *tm)
{
    long long year = (long long)tm->tm_year + 1900;
    long long mon = tm->tm_mon;
    year += mon / 12;
    mon %= 12;
    if (mon < 0) { mon += 12; year--; }
    long long days = __cervus_days_from_civil(year, (int)mon + 1, 1) + (long long)tm->tm_mday - 1;
    return days * 86400 + (long long)tm->tm_hour * 3600 + (long long)tm->tm_min * 60 + tm->tm_sec;
}

time_t timegm(struct tm *tm)
{
    if (!tm) { errno = EINVAL; return (time_t)-1; }
    time_t t = (time_t)fields_to_seconds(tm);
    struct tm norm;
    if (!gmtime_r(&t, &norm)) return (time_t)-1;
    *tm = norm;
    return t;
}

time_t mktime(struct tm *tm)
{
    if (!tm) { errno = EINVAL; return (time_t)-1; }
    time_t t = (time_t)(fields_to_seconds(tm) - timezone_offset());
    struct tm norm;
    if (!localtime_r(&t, &norm)) return (time_t)-1;
    *tm = norm;
    return t;
}
