#include <utime.h>
#include <sys/stat.h>
#include <time.h>

int utime(const char *path, const struct utimbuf *times)
{
    if (times) return utimes_at(path, (int64_t)times->actime, (int64_t)times->modtime);
    time_t now = time(0);
    return utimes_at(path, (int64_t)now, (int64_t)now);
}
