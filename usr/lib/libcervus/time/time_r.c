#include <time.h>

extern long timezone_offset(void);

struct tm *localtime_r(const time_t *t, struct tm *out)
{
    if (!t) return gmtime_r(t, out);
    time_t local = *t + timezone_offset();
    return gmtime_r(&local, out);
}

double difftime(time_t a, time_t b) { return (double)(a - b); }
