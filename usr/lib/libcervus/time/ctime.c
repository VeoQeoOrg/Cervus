#include <time.h>

char *ctime_r(const time_t *t, char *buf)
{
    struct tm tm;
    if (!localtime_r(t, &tm)) return NULL;
    return asctime_r(&tm, buf);
}

char *ctime(const time_t *t)
{
    struct tm *tm = localtime(t);
    return tm ? asctime(tm) : NULL;
}
