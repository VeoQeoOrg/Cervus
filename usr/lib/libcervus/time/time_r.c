#include <time.h>
#include <string.h>

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    struct tm *r = gmtime(t);
    if (!r || !out) return 0;
    memcpy(out, r, sizeof *out);
    return out;
}

struct tm *localtime_r(const time_t *t, struct tm *out)
{
    struct tm *r = localtime(t);
    if (!r || !out) return 0;
    memcpy(out, r, sizeof *out);
    return out;
}

double difftime(time_t a, time_t b) { return (double)(a - b); }
