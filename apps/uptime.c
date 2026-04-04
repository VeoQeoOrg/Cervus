#include "../apps/cervus_user.h"

static void print_pad2(uint64_t v){
    if(v<10) write(1,"0",1);
    print_u64(v);}

CERVUS_MAIN(uptime_main) {
    (void)argc; (void)argv;
    uint64_t ns = uptime_ns();
    uint64_t total_s  = ns / 1000000000ULL;
    uint64_t ms       = (ns / 1000000ULL) % 1000ULL;
    uint64_t secs     = total_s % 60;
    uint64_t mins     = (total_s / 60) % 60;
    uint64_t hours    = (total_s / 3600) % 24;
    uint64_t days     = total_s / 86400;
    ws("  Uptime: ");
    if(days>0){ print_u64(days); ws(" day"); if(days!=1)ws("s"); ws(", "); }
    print_pad2(hours); ws(":"); print_pad2(mins); ws(":"); print_pad2(secs);
    ws("  ("); print_u64(total_s); ws("s  ");
    print_u64(ms); ws("ms)\n");
    exit(0);
}