#include "../apps/cervus_user.h"

static const char *logo[] = {
    "    L          ",
    "   'k.i ,      ",
    "    \";\"+U.,    ",
    "       \\_' -.  ",
    "      .f  ,_.;.",
    "      I ,f     ",
    "       '       ",
    NULL
};

static void cpuid_leaf(uint32_t leaf,
                       uint32_t *a, uint32_t *b,
                       uint32_t *c, uint32_t *d) {
    asm volatile("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "0"(leaf), "2"(0));
}

static void print_uptime(void) {
    uint64_t ns      = uptime_ns();
    uint64_t total_s = ns / 1000000000ULL;
    uint64_t ms      = (ns / 1000000ULL) % 1000ULL;
    uint64_t secs    = total_s % 60;
    uint64_t mins    = (total_s / 60) % 60;
    uint64_t hours   = (total_s / 3600) % 24;
    uint64_t days    = total_s / 86400;
    ws("uptime: ");
    if (days > 0) printf("%llud, ", (unsigned long long)days);
    printf("%02llu:%02llu:%02llu  (%llus %llums)",
           (unsigned long long)hours,
           (unsigned long long)mins,
           (unsigned long long)secs,
           (unsigned long long)total_s,
           (unsigned long long)ms);
}

static void print_cpu(void) {
    uint32_t a, b, c, d;
    cpuid_leaf(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        char brand[49]; uint32_t *p = (uint32_t *)brand;
        cpuid_leaf(0x80000002, &p[0], &p[1], &p[2],  &p[3]);
        cpuid_leaf(0x80000003, &p[4], &p[5], &p[6],  &p[7]);
        cpuid_leaf(0x80000004, &p[8], &p[9], &p[10], &p[11]);
        brand[48] = '\0';
        const char *br = brand;
        while (*br == ' ') br++;
        ws("cpu: "); ws(br);
    }
}

static void print_mem(void) {
    cervus_meminfo_t mi;
    if (meminfo(&mi) != 0) return;
    uint64_t used  = mi.used_bytes;
    uint64_t total = mi.total_bytes;
    const uint64_t MiB = 1024ULL * 1024;
    const uint64_t GiB = 1024ULL * 1024 * 1024;
    ws("mem: ");
    if (total >= GiB)
        printf("%llu.%02llu / %llu.%02llu GiB",
               (unsigned long long)(used  / GiB), (unsigned long long)((used  % GiB) * 100 / GiB),
               (unsigned long long)(total / GiB), (unsigned long long)((total % GiB) * 100 / GiB));
    else
        printf("%llu / %llu MiB",
               (unsigned long long)(used  / MiB),
               (unsigned long long)(total / MiB));
}

CERVUS_MAIN(fetch_main) {
    wn();
    for (int i = 0; logo[i]; i++) {
        ws(" ");
        ws(logo[i]);
        ws("  ");
        switch (i) {
            case 1: ws("os: Cervus OS");      break;
            case 2: print_uptime();            break;
            case 3: print_cpu();               break;
            case 4: ws(C_RESET "shell: CSH"); break;
            case 5: print_mem();               break;
        }
        wn();
    }
    wn();
}