#include <stdio.h>
#include <sys/cervus.h>

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint64_t ns      = cervus_uptime_ns();
    uint64_t total_s = ns / 1000000000ULL;
    uint64_t ms      = (ns / 1000000ULL) % 1000ULL;
    uint64_t secs    = total_s % 60;
    uint64_t mins    = (total_s / 60) % 60;
    uint64_t hours   = (total_s / 3600) % 24;
    uint64_t days    = total_s / 86400;

    fputs("  Uptime: ", stdout);
    if (days > 0)
        printf("%lu day%s, ", (unsigned long)days, days != 1 ? "s" : "");
    printf("%02lu:%02lu:%02lu  (%lus  %lums)\n",
           (unsigned long)hours, (unsigned long)mins, (unsigned long)secs,
           (unsigned long)total_s, (unsigned long)ms);
    return 0;
}
