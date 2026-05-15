#include <stdio.h>
#include <stdint.h>
#include <sys/cervus.h>

char *tmpnam(char *buf)
{
    static char sbuf[32];
    static uint64_t seq = 0;
    char *out = buf ? buf : sbuf;
    uint64_t seed = cervus_uptime_ns() ^ (seq++);
    snprintf(out, 32, "/tmp/tmp%llu", (unsigned long long)seed);
    return out;
}
