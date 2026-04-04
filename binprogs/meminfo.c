#include "../apps/cervus_user.h"

static void print_size(uint64_t bytes) {
    if (bytes >= 1024ULL*1024*1024) {
        uint64_t w = bytes / (1024ULL*1024*1024);
        uint64_t f = (bytes % (1024ULL*1024*1024)) * 100 / (1024ULL*1024*1024);
        print_u64(w); ws("."); if(f<10)ws("0"); print_u64(f); ws(" GiB");
    } else if (bytes >= 1024ULL*1024) {
        uint64_t w = bytes / (1024ULL*1024);
        uint64_t f = (bytes % (1024ULL*1024)) * 100 / (1024ULL*1024);
        print_u64(w); ws("."); if(f<10)ws("0"); print_u64(f); ws(" MiB");
    } else {
        print_u64(bytes / 1024); ws(" KiB");
    }
}

static void print_bar(uint64_t used, uint64_t total) {
    int pct = (total > 0) ? (int)(used * 100 / total) : 0;
    int fill = pct / 5;
    ws("  [");
    for (int i = 0; i < 20; i++) {
        if (i < fill)       ws(C_RED "#" C_RESET);
        else if (i == fill) ws(C_YELLOW "#" C_RESET);
        else                ws(C_GRAY "." C_RESET);
    }
    ws("] ");
    print_u64(pct);
    ws("%\n");
}

CERVUS_MAIN(meminfo_main) {
    (void)argc; (void)argv;
    wn();
    ws("  " C_CYAN "Memory Info" C_RESET "\n");
    ws("  " C_GRAY "-------------------------" C_RESET "\n");

    cervus_meminfo_t mi;
    if (meminfo(&mi) == 0) {
        ws("  Total:  "); print_size(mi.total_bytes);  wn();
        ws("  Used:   "); print_size(mi.used_bytes);   wn();
        ws("  Free:   "); print_size(mi.free_bytes);   wn();
        ws("  " C_GRAY "-------------------------" C_RESET "\n");
        print_bar(mi.used_bytes, mi.total_bytes);
    } else {
        ws("  meminfo syscall not available\n");
        ws("  Heap:   0x"); print_hex((uint64_t)(uintptr_t)sbrk(0)); wn();
        uint64_t up = uptime_ns();
        ws("  Uptime: "); print_u64(up / 1000000000ULL);
        ws("s "); print_u64((up / 1000000ULL) % 1000ULL); ws("ms\n");
    }
    wn();
    exit(0);
}