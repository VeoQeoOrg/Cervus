#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <time.h>

struct timeval {
    time_t tv_sec;
    long   tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);
int utimes(const char *path, const struct timeval times[2]);

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);
int getitimer(int which, struct itimerval *curr_value);

#define timerisset(tv) ((tv)->tv_sec || (tv)->tv_usec)
#define timerclear(tv) ((tv)->tv_sec = (tv)->tv_usec = 0)
#define timercmp(a, b, CMP)                                   \
    (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec CMP (b)->tv_usec) \
                                  : ((a)->tv_sec CMP (b)->tv_sec))
#define timeradd(a, b, res)                                   \
    do {                                                      \
        (res)->tv_sec = (a)->tv_sec + (b)->tv_sec;            \
        (res)->tv_usec = (a)->tv_usec + (b)->tv_usec;         \
        if ((res)->tv_usec >= 1000000) {                      \
            (res)->tv_sec++;                                  \
            (res)->tv_usec -= 1000000;                        \
        }                                                     \
    } while (0)
#define timersub(a, b, res)                                   \
    do {                                                      \
        (res)->tv_sec = (a)->tv_sec - (b)->tv_sec;            \
        (res)->tv_usec = (a)->tv_usec - (b)->tv_usec;         \
        if ((res)->tv_usec < 0) {                             \
            (res)->tv_sec--;                                  \
            (res)->tv_usec += 1000000;                        \
        }                                                     \
    } while (0)

#ifdef __cplusplus
}
#endif
#endif
