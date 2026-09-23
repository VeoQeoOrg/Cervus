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

struct timespec {
    time_t  tv_sec;
    long    tv_nsec;
};

time_t time(time_t *t);
clock_t clock(void);

struct tm *localtime(const time_t *t);
long        timezone_offset(void);
const char *timezone_name(void);
struct tm *gmtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime_r(const time_t *t, struct tm *out);
double     difftime(time_t a, time_t b);
time_t mktime(struct tm *tm);

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
char  *asctime(const struct tm *tm);
char  *ctime(const time_t *t);

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

#ifdef __cplusplus
}
#endif
#endif