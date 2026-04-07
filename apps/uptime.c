#include "../apps/cervus_user.h"

CERVUS_MAIN(uptime_main) {
    (void)argc; (void)argv;
    uint64_t ns     = uptime_ns();
    uint64_t total_s = ns / 1000000000ULL;
    uint64_t ms      = (ns / 1000000ULL) % 1000ULL;
    uint64_t secs    = total_s % 60;
    uint64_t mins    = (total_s / 60) % 60;
    uint64_t hours   = (total_s / 3600) % 24;
    uint64_t days    = total_s / 86400;

    ws("  Uptime: ");
    if (days > 0) printf("%llu day%s, ", (unsigned long long)days, days != 1 ? "s" : "");
    printf("%02llu:%02llu:%02llu  (%llus  %llums)\n",
           (unsigned long long)hours,
           (unsigned long long)mins,
           (unsigned long long)secs,
           (unsigned long long)total_s,
           (unsigned long long)ms);
    exit(0);
}