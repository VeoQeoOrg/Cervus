#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

int utimes(const char *path, const struct timeval times[2])
{
    if (!times) {
        int64_t now = (int64_t)time(0);
        return utimes_at(path, now, now);
    }
    return utimes_at(path, (int64_t)times[0].tv_sec, (int64_t)times[1].tv_sec);
}
