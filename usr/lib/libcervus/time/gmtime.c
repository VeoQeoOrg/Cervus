#include <time.h>

extern int __cervus_is_leap(int y);
extern const int __cervus_days_in_mon[2][12];

static struct tm __tm_buf;

struct tm *gmtime(const time_t *t)
{
    if (!t) return (void *)0;
    long long sec = (long long)*t;
    long days = (long)(sec / 86400);
    long rem  = (long)(sec % 86400);
    if (rem < 0) { rem += 86400; days--; }

    __tm_buf.tm_hour = rem / 3600;
    rem -= __tm_buf.tm_hour * 3600;
    __tm_buf.tm_min  = rem / 60;
    __tm_buf.tm_sec  = rem - __tm_buf.tm_min * 60;

    __tm_buf.tm_wday = (int)((days + 4) % 7);
    if (__tm_buf.tm_wday < 0) __tm_buf.tm_wday += 7;

    int year = 1970;
    while (1) {
        int ly = __cervus_is_leap(year);
        int dy = ly ? 366 : 365;
        if (days >= dy) { days -= dy; year++; }
        else if (days < 0) { year--; days += __cervus_is_leap(year) ? 366 : 365; }
        else break;
    }
    __tm_buf.tm_year = year - 1900;
    __tm_buf.tm_yday = (int)days;
    int ly = __cervus_is_leap(year);
    int m = 0;
    while (m < 12 && days >= __cervus_days_in_mon[ly][m]) {
        days -= __cervus_days_in_mon[ly][m];
        m++;
    }
    __tm_buf.tm_mon  = m;
    __tm_buf.tm_mday = (int)days + 1;
    __tm_buf.tm_isdst = 0;
    return &__tm_buf;
}
