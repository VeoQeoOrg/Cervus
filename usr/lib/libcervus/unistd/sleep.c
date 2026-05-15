#include <unistd.h>
#include <stdint.h>
#include <sys/cervus.h>

unsigned int sleep(unsigned int sec)
{
    cervus_nanosleep((uint64_t)sec * 1000000000ULL);
    return 0;
}
