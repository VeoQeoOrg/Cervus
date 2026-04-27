#ifndef _TIME_H
#define _TIME_H

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
struct tm *gmtime(const time_t *t);
time_t mktime(struct tm *tm);

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
char  *asctime(const struct tm *tm);
char  *ctime(const time_t *t);

int nanosleep(const struct timespec *req, struct timespec *rem);

#define CLOCK_REALTIME   0
#define CLOCK_MONOTONIC  1
int clock_gettime(int clk, struct timespec *tp);

static inline long difftime_l(time_t a, time_t b) { return (long)(a - b); }

#endif