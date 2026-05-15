#include <time.h>

static const char *__wday_name[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *__mon_name[]  = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};

static char __asctime_buf[32];

char *asctime(const struct tm *tm)
{
    if (!tm) return (void *)0;
    int wday = tm->tm_wday; if (wday < 0 || wday > 6) wday = 0;
    int mon  = tm->tm_mon;  if (mon  < 0 || mon  > 11) mon  = 0;
    int y = tm->tm_year + 1900;
    int pos = 0;
    const char *w = __wday_name[wday];
    const char *mn = __mon_name[mon];
    __asctime_buf[pos++] = w[0];  __asctime_buf[pos++] = w[1];  __asctime_buf[pos++] = w[2];
    __asctime_buf[pos++] = ' ';
    __asctime_buf[pos++] = mn[0]; __asctime_buf[pos++] = mn[1]; __asctime_buf[pos++] = mn[2];
    __asctime_buf[pos++] = ' ';
    int md = tm->tm_mday;
    __asctime_buf[pos++] = (char)('0' + (md/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (md % 10));
    __asctime_buf[pos++] = ' ';
    int hh = tm->tm_hour, mm = tm->tm_min, ss = tm->tm_sec;
    __asctime_buf[pos++] = (char)('0' + (hh/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (hh % 10));
    __asctime_buf[pos++] = ':';
    __asctime_buf[pos++] = (char)('0' + (mm/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (mm % 10));
    __asctime_buf[pos++] = ':';
    __asctime_buf[pos++] = (char)('0' + (ss/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (ss % 10));
    __asctime_buf[pos++] = ' ';
    __asctime_buf[pos++] = (char)('0' + (y/1000 % 10));
    __asctime_buf[pos++] = (char)('0' + (y/100 % 10));
    __asctime_buf[pos++] = (char)('0' + (y/10 % 10));
    __asctime_buf[pos++] = (char)('0' + (y % 10));
    __asctime_buf[pos++] = '\n';
    __asctime_buf[pos]   = '\0';
    return __asctime_buf;
}
