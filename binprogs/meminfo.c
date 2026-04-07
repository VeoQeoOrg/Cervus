#include "../apps/cervus_user.h"

static void print_size(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024 * 1024) {
        uint64_t w = bytes / (1024ULL * 1024 * 1024);
        uint64_t f = (bytes % (1024ULL * 1024 * 1024)) * 100 / (1024ULL * 1024 * 1024);
        printf("%llu.%02llu GiB", (unsigned long long)w, (unsigned long long)f);
    } else if (bytes >= 1024ULL * 1024) {
        uint64_t w = bytes / (1024ULL * 1024);
        uint64_t f = (bytes % (1024ULL * 1024)) * 100 / (1024ULL * 1024);
        printf("%llu.%02llu MiB", (unsigned long long)w, (unsigned long long)f);
    } else {
        printf("%llu KiB", (unsigned long long)(bytes / 1024));
    }
}

static void print_bar(uint64_t used, uint64_t total) {
    int pct  = (total > 0) ? (int)(used * 100 / total) : 0;
    int fill = pct / 5;
    ws("  [");
    for (int i = 0; i < 20; i++) {
        if (i < fill)       ws(C_RED "#" C_RESET);
        else if (i == fill) ws(C_YELLOW "#" C_RESET);
        else                ws(C_GRAY "." C_RESET);
    }
    printf("] %d%%\n", pct);
}

CERVUS_MAIN(meminfo_main) {
    (void)argc; (void)argv;
    wn();
    ws("  " C_CYAN "Memory Info" C_RESET "\n");
    ws("  " C_GRAY "-------------------------" C_RESET "\n");

    cervus_meminfo_t mi;
    if (meminfo(&mi) == 0) {
        ws("  Total:  "); print_size(mi.total_bytes); wn();
        ws("  Used:   "); print_size(mi.used_bytes);  wn();
        ws("  Free:   "); print_size(mi.free_bytes);  wn();
        ws("  " C_GRAY "-------------------------" C_RESET "\n");
        print_bar(mi.used_bytes, mi.total_bytes);
    } else {
        ws("  meminfo syscall not available\n");
        printf("  Heap:   %p\n", sbrk(0));
        uint64_t up = uptime_ns();
        printf("  Uptime: %llus %llums\n",
               (unsigned long long)(up / 1000000000ULL),
               (unsigned long long)((up / 1000000ULL) % 1000ULL));
    }
    wn();
    exit(0);
}