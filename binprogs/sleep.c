#include "../apps/cervus_user.h"

static uint64_t parse_uint(const char *s){
    uint64_t v=0;
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return v;}

CERVUS_MAIN(sleep_main) {
    const char *secs_arg = (void*)0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        secs_arg = argv[i]; break;
    }
    if (!secs_arg) { ws("Usage: sleep <seconds>\n"); exit(1); }
    uint64_t secs=parse_uint(secs_arg);
    nanosleep_simple(secs * 1000000000ULL);
    exit(0);
}