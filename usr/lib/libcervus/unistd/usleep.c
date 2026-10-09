#include <unistd.h>
#include <time.h>

int usleep(unsigned int usec)
{
    struct timespec ts = { (time_t)(usec / 1000000), (long)(usec % 1000000) * 1000 };
    return nanosleep(&ts, &ts);
}
