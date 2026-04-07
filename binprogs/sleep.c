#include "../apps/cervus_user.h"

CERVUS_MAIN(sleep_main) {
    const char *secs_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        secs_arg = argv[i]; break;
    }
    if (!secs_arg) { ws("Usage: sleep <seconds>\n"); exit(1); }
    uint64_t secs = strtoul(secs_arg, NULL, 10);
    nanosleep_simple(secs * 1000000000ULL);
    exit(0);
}