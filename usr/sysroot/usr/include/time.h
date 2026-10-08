#ifndef _TIME_H
#define _TIME_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/types.h>

typedef long clock_t;

#define CLOCKS_PER_SEC 1000000L

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

#ifndef __CERVUS_STRUCT_TIMESPEC
#define __CERVUS_STRUCT_TIMESPEC
struct timespec {
    time_t  tv_sec;
    long    tv_nsec;
};
#endif

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

time_t time(time_t *t);
clock_t clock(void);

struct tm *localtime(const time_t *t);
void        tzset(void);
extern char *tzname[2];
extern long  timezone;
extern int   daylight;
struct tm *gmtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime_r(const time_t *t, struct tm *out);
double     difftime(time_t a, time_t b);
time_t mktime(struct tm *tm);

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
char  *asctime(const struct tm *tm);
char  *ctime(const time_t *t);
char  *asctime_r(const struct tm *__restrict tm, char *__restrict buf);
char  *ctime_r(const time_t *t, char *buf);
char  *strptime(const char *__restrict s, const char *__restrict fmt, struct tm *__restrict tm);

int nanosleep(const struct timespec *req, struct timespec *rem);

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_REALTIME_COARSE    5
#define CLOCK_MONOTONIC_COARSE   6
#define CLOCK_BOOTTIME           7
int clock_gettime(clockid_t clk, struct timespec *tp);
int clock_getres(clockid_t clk, struct timespec *res);

static inline long difftime_l(time_t a, time_t b) { return (long)(a - b); }

#define TIMER_ABSTIME 1
int clock_nanosleep(clockid_t clk, int flags, const struct timespec *req, struct timespec *rem);

time_t timegm(struct tm *tm);
int clock_settime(clockid_t clk, const struct timespec *ts);

#ifdef __cplusplus
}
#endif
#endif