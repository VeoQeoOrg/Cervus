#include <unistd.h>
#include <time.h>

unsigned int sleep(unsigned int sec)
{
    struct timespec ts = { (time_t)sec, 0 };
    if (nanosleep(&ts, &ts)) return (unsigned int)ts.tv_sec;
    return 0;
}
