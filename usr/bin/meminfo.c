#include <stdio.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static void print_size(uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024 * 1024) {
        uint64_t w = bytes / (1024ULL * 1024 * 1024);
        uint64_t f = (bytes % (1024ULL * 1024 * 1024)) * 100 / (1024ULL * 1024 * 1024);
        printf("%lu.%02lu GiB", (unsigned long)w, (unsigned long)f);
    } else if (bytes >= 1024ULL * 1024) {
        uint64_t w = bytes / (1024ULL * 1024);
        uint64_t f = (bytes % (1024ULL * 1024)) * 100 / (1024ULL * 1024);
        printf("%lu.%02lu MiB", (unsigned long)w, (unsigned long)f);
    } else {
        printf("%lu KiB", (unsigned long)(bytes / 1024));
    }
}

static void print_bar(uint64_t used, uint64_t total)
{
    int pct = (total > 0) ? (int)(used * 100 / total) : 0;
    int fill = pct / 5;
    fputs("  [", stdout);
    for (int i = 0; i < 20; i++) {
        if (i < fill)       fputs(C_RED "#" C_RESET, stdout);
        else if (i == fill) fputs(C_YELLOW "#" C_RESET, stdout);
        else                fputs(C_GRAY "." C_RESET, stdout);
    }
    printf("] %d%%\n", pct);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    putchar('\n');
    fputs("  " C_CYAN "Memory Info" C_RESET "\n", stdout);
    fputs("  " C_GRAY "-------------------------" C_RESET "\n", stdout);

    cervus_meminfo_t mi;
    if (cervus_meminfo(&mi) == 0) {
        fputs("  Total:  ", stdout); print_size(mi.total_bytes); putchar('\n');
        fputs("  Used:   ", stdout); print_size(mi.used_bytes);  putchar('\n');
        fputs("  Free:   ", stdout); print_size(mi.free_bytes);  putchar('\n');
        fputs("  " C_GRAY "-------------------------" C_RESET "\n", stdout);
        print_bar(mi.used_bytes, mi.total_bytes);
    } else {
        fputs("  meminfo syscall not available\n", stdout);
        printf("  Heap:   %p\n", sbrk(0));
        uint64_t up = cervus_uptime_ns();
        printf("  Uptime: %lus %lums\n",
               (unsigned long)(up / 1000000000ULL),
               (unsigned long)((up / 1000000ULL) % 1000ULL));
    }
    putchar('\n');
    return 0;
}
