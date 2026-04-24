#include <stdio.h>
#include <stdint.h>
#include <sys/cervus.h>
#include <cervus_util.h>

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

static void cpuid_leaf(uint32_t leaf, uint32_t *a, uint32_t *b,
                       uint32_t *c, uint32_t *d)
{
    asm volatile ("cpuid"
                  : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                  : "0"(leaf), "2"(0));
}

static void print_uptime(void)
{
    uint64_t ns      = cervus_uptime_ns();
    uint64_t total_s = ns / 1000000000ULL;
    uint64_t ms      = (ns / 1000000ULL) % 1000ULL;
    uint64_t secs    = total_s % 60;
    uint64_t mins    = (total_s / 60) % 60;
    uint64_t hours   = (total_s / 3600) % 24;
    uint64_t days    = total_s / 86400;
    fputs("uptime: ", stdout);
    if (days > 0) printf("%lud, ", (unsigned long)days);
    printf("%02lu:%02lu:%02lu  (%lus %lums)",
           (unsigned long)hours, (unsigned long)mins, (unsigned long)secs,
           (unsigned long)total_s, (unsigned long)ms);
}

static void print_cpu(void)
{
    uint32_t a, b, c, d;
    cpuid_leaf(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        char brand[49];
        uint32_t *p = (uint32_t *)brand;
        cpuid_leaf(0x80000002, &p[0], &p[1], &p[2],  &p[3]);
        cpuid_leaf(0x80000003, &p[4], &p[5], &p[6],  &p[7]);
        cpuid_leaf(0x80000004, &p[8], &p[9], &p[10], &p[11]);
        brand[48] = '\0';
        const char *br = brand;
        while (*br == ' ') br++;
        printf("cpu: %s", br);
    }
}

static void print_mem(void)
{
    cervus_meminfo_t mi;
    if (cervus_meminfo(&mi) != 0) return;
    uint64_t used  = mi.used_bytes;
    uint64_t total = mi.total_bytes;
    const uint64_t MiB = 1024ULL * 1024;
    const uint64_t GiB = 1024ULL * 1024 * 1024;
    fputs("mem: ", stdout);
    if (total >= GiB)
        printf("%lu.%02lu / %lu.%02lu GiB",
               (unsigned long)(used / GiB),  (unsigned long)((used  % GiB) * 100 / GiB),
               (unsigned long)(total / GiB), (unsigned long)((total % GiB) * 100 / GiB));
    else
        printf("%lu / %lu MiB",
               (unsigned long)(used / MiB), (unsigned long)(total / MiB));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    putchar('\n');
    for (int i = 0; logo[i]; i++) {
        printf(" %s  ", logo[i]);
        switch (i) {
            case 1: fputs("os: Cervus OS", stdout);     break;
            case 2: print_uptime();                     break;
            case 3: print_cpu();                        break;
            case 4: fputs(C_RESET "shell: CSH", stdout); break;
            case 5: print_mem();                        break;
        }
        putchar('\n');
    }
    putchar('\n');
    return 0;
}
