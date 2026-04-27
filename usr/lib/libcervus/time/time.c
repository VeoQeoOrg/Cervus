#include <time.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

extern int __cervus_errno;

typedef struct { int64_t tv_sec; int64_t tv_nsec; } __cervus_ts_raw_t;

time_t time(time_t *t)
{
    __cervus_ts_raw_t ts = {0, 0};
    syscall2(SYS_CLOCK_GET, 0, &ts);
    time_t v = (time_t)ts.tv_sec;
    if (t) *t = v;
    return v;
}

int clock_gettime(int clk, struct timespec *tp)
{
    if (!tp) { __cervus_errno = EINVAL; return -1; }
    __cervus_ts_raw_t ts = {0, 0};
    long r = syscall2(SYS_CLOCK_GET, clk, &ts);
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    tp->tv_sec  = (time_t)ts.tv_sec;
    tp->tv_nsec = (long)ts.tv_nsec;
    return 0;
}

clock_t clock(void)
{
    uint64_t up_ns = (uint64_t)syscall0(SYS_UPTIME);
    return (clock_t)(up_ns / 1000ULL);
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    (void)tz;
    if (!tv) { __cervus_errno = EINVAL; return -1; }
    __cervus_ts_raw_t ts = {0, 0};
    syscall2(SYS_CLOCK_GET, 0, &ts);
    tv->tv_sec  = (time_t)ts.tv_sec;
    tv->tv_usec = (long)(ts.tv_nsec / 1000);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req) { __cervus_errno = EINVAL; return -1; }
    uint64_t ns = (uint64_t)req->tv_sec * 1000000000ULL + (uint64_t)req->tv_nsec;
    long r = syscall1(SYS_SLEEP_NS, ns);
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    return 0;
}

static int __is_leap(int y)
{
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static const int __days_in_mon[2][12] = {
    {31,28,31,30,31,30,31,31,30,31,30,31},
    {31,29,31,30,31,30,31,31,30,31,30,31},
};

static struct tm __tm_buf;

struct tm *gmtime(const time_t *t)
{
    if (!t) return NULL;
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
        int ly = __is_leap(year);
        int dy = ly ? 366 : 365;
        if (days >= dy) { days -= dy; year++; }
        else if (days < 0) { year--; days += __is_leap(year) ? 366 : 365; }
        else break;
    }
    __tm_buf.tm_year = year - 1900;
    __tm_buf.tm_yday = (int)days;
    int ly = __is_leap(year);
    int m = 0;
    while (m < 12 && days >= __days_in_mon[ly][m]) {
        days -= __days_in_mon[ly][m];
        m++;
    }
    __tm_buf.tm_mon  = m;
    __tm_buf.tm_mday = (int)days + 1;
    __tm_buf.tm_isdst = 0;
    return &__tm_buf;
}

struct tm *localtime(const time_t *t) { return gmtime(t); }

time_t mktime(struct tm *tm)
{
    if (!tm) return (time_t)-1;
    int year = tm->tm_year + 1900;
    int mon  = tm->tm_mon;
    long days = 0;
    for (int y = 1970; y < year; y++) days += __is_leap(y) ? 366 : 365;
    int ly = __is_leap(year);
    for (int m = 0; m < mon; m++) days += __days_in_mon[ly][m];
    days += tm->tm_mday - 1;
    long long sec = (long long)days * 86400LL
                  + (long long)tm->tm_hour * 3600LL
                  + (long long)tm->tm_min * 60LL
                  + (long long)tm->tm_sec;
    return (time_t)sec;
}

static const char *__wday_name[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *__mon_name[]  = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};

static char __asctime_buf[32];

char *asctime(const struct tm *tm)
{
    if (!tm) return NULL;
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

char *ctime(const time_t *t) { return asctime(gmtime(t)); }

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    if (!s || !fmt || !tm || max == 0) return 0;
    size_t i = 0;
    while (*fmt && i + 1 < max) {
        if (*fmt != '%') { s[i++] = *fmt++; continue; }
        fmt++;
        char tmp[16];
        int n = 0;
        switch (*fmt) {
            case 'Y': {
                int y = tm->tm_year + 1900;
                n = 4;
                tmp[0] = (char)('0' + (y/1000)%10);
                tmp[1] = (char)('0' + (y/100)%10);
                tmp[2] = (char)('0' + (y/10)%10);
                tmp[3] = (char)('0' + y%10);
                break;
            }
            case 'm': { int v=tm->tm_mon+1; tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case 'd': { int v=tm->tm_mday;  tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case 'H': { int v=tm->tm_hour;  tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case 'M': { int v=tm->tm_min;   tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case 'S': { int v=tm->tm_sec;   tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case '%': tmp[0]='%'; n=1; break;
            default:  tmp[0]='%'; tmp[1]=*fmt; n = (*fmt ? 2 : 1); break;
        }
        for (int k = 0; k < n && i + 1 < max; k++) s[i++] = tmp[k];
        if (*fmt) fmt++;
    }
    s[i] = '\0';
    return i;
}