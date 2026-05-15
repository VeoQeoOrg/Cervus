#include <time.h>

extern int __cervus_is_leap(int y);
extern const int __cervus_days_in_mon[2][12];

time_t mktime(struct tm *tm)
{
    if (!tm) return (time_t)-1;
    int year = tm->tm_year + 1900;
    int mon  = tm->tm_mon;
    long days = 0;
    for (int y = 1970; y < year; y++) days += __cervus_is_leap(y) ? 366 : 365;
    int ly = __cervus_is_leap(year);
    for (int m = 0; m < mon; m++) days += __cervus_days_in_mon[ly][m];
    days += tm->tm_mday - 1;
    long long sec = (long long)days * 86400LL
                  + (long long)tm->tm_hour * 3600LL
                  + (long long)tm->tm_min * 60LL
                  + (long long)tm->tm_sec;
    return (time_t)sec;
}
