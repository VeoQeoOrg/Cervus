#include "../apps/cervus_user.h"

static uint64_t parse_uint(const char *s){
    uint64_t v=0;
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return v;}

CERVUS_MAIN(sleep_main) {
    if(argc<2){
        ws("Usage: sleep <seconds>\n");
        exit(1);
    }
    uint64_t secs=parse_uint(argv[1]);
    nanosleep_simple(secs * 1000000000ULL);
    exit(0);
}