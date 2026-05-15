#include <unistd.h>
#include <stdint.h>
#include <sys/cervus.h>

int usleep(unsigned int usec)
{
    return cervus_nanosleep((uint64_t)usec * 1000ULL);
}
