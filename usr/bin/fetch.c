#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/cervus.h>
#include <cervus_util.h>
#include <sys/netcfg.h>

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

static const char *get_shell(void)
{
    const char *s = getenv("SHELL");
    if (s && s[0]) return s;
    static char buf[256];
    int fd = open("/etc/shell", O_RDONLY, 0);
    if (fd < 0) fd = open("/mnt/etc/shell", O_RDONLY, 0);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            int i = 0;
            while (buf[i] && buf[i] != '\n' && buf[i] != '\r') i++;
            buf[i] = '\0';
            if (buf[0]) return buf;
        }
    }
    return "/bin/csh";
}

static void print_shell(void)
{
    fputs(C_RESET "shell: ", stdout);
    fputs(get_shell(), stdout);
}

static void print_kernel(void)
{
    fputs(C_RESET "kernel: ", stdout);
    char buf[128];
    int fd = open("/proc/version", O_RDONLY, 0);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n > 0) {
            buf[n] = 0;
            char *nl = strchr(buf, '\n');
            if (nl) *nl = 0;
            fputs(buf, stdout);
            return;
        }
    }
    fputs("Cervus", stdout);
}

static void print_cpus(void)
{
    fputs(C_RESET "cores: ", stdout);
    char buf[512];
    int fd = open("/proc/cpuinfo", O_RDONLY, 0);
    int total = 0, online = 0;
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n > 0) {
            buf[n] = 0;
            char *p = strstr(buf, "cpus:");
            if (p) total = atoi(p + 5);
            p = strstr(buf, "online:");
            if (p) online = atoi(p + 7);
        }
    }
    if (total > 0) {
        if (online && online != total) printf("%d of %d online", online, total);
        else                           printf("%d", total);
    } else {
        fputs("?", stdout);
    }
}

static void print_host(void)
{
    fputs(C_RESET "host: ", stdout);
    char buf[80];
    int fd = open("/etc/hostname", O_RDONLY, 0);
    if (fd < 0) fd = open("/mnt/etc/hostname", O_RDONLY, 0);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n > 0) {
            buf[n] = 0;
            char *nl = strchr(buf, '\n'); if (nl) *nl = 0;
            if (buf[0]) { fputs(buf, stdout); return; }
        }
    }
    fputs("cervus", stdout);
}

static void print_net(void)
{
    fputs(C_RESET "net: ", stdout);
    net_ifcfg_t c;
    int shown = 0;
    for (int i = 0; i < 8; i++) {
        if (netif_get(i, &c) != 0) break;
        if (!c.ip) continue;
        if (shown++) fputs(", ", stdout);
        printf("%s %u.%u.%u.%u", c.name,
               (c.ip >> 24) & 0xFF, (c.ip >> 16) & 0xFF,
               (c.ip >> 8) & 0xFF, c.ip & 0xFF);
    }
    if (!shown) fputs("no address", stdout);
}

static void print_disks(void)
{
    fputs(C_RESET "disks: ", stdout);
    cervus_disk_info_t d;
    int shown = 0;
    for (int i = 0; i < 8; i++) {
        if (cervus_disk_info(i, &d) != 0) break;
        if (!d.name[0]) continue;
        if (shown++) fputs(", ", stdout);
        unsigned long gb = (unsigned long)(d.size_bytes / (1024ull * 1024ull * 1024ull));
        if (gb) printf("%s %luG", d.name, gb);
        else    printf("%s %luM", d.name,
                       (unsigned long)(d.size_bytes / (1024ull * 1024ull)));
        if (shown >= 3) break;
    }
    if (!shown) fputs("none", stdout);
}

static void print_term(void)
{
    fputs(C_RESET "display: ", stdout);
    cervus_fb_info_t fb;
    if (cervus_fb_info(&fb) == 0) printf("%ux%u %u bpp", fb.width, fb.height, fb.bpp);
    else fputs("text", stdout);
}

static void print_palette(void)
{
    fputs(C_RESET, stdout);
    for (int c = 0; c < 8; c++) printf("\x1b[4%dm   \x1b[0m", c);
    putchar('\n');
    for (int i = 0; i < 17; i++) putchar(' ');
    for (int c = 0; c < 8; c++) printf("\x1b[10%dm   \x1b[0m", c);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    putchar('\n');
    for (int i = 0; logo[i]; i++) {
        printf(" %s  ", logo[i]);
        switch (i) {
            case 0: print_host();                       break;
            case 1: fputs("os: Cervus OS v0.0.2", stdout); break;
            case 2: print_kernel();                     break;
            case 3: print_uptime();                     break;
            case 4: print_cpu();                        break;
            case 5: print_cpus();                       break;
            case 6: print_mem();                        break;
        }
        putchar('\n');
    }
    for (int pass = 0; pass < 4; pass++) {
        for (int i = 0; i < 17; i++) putchar(' ');
        switch (pass) {
            case 0: print_shell();   break;
            case 1: print_term();    break;
            case 2: print_net();     break;
            case 3: print_disks();   break;
        }
        putchar('\n');
    }
    for (int i = 0; i < 17; i++) putchar(' ');
    print_palette();
    fputs(C_RESET "\n\n", stdout);
    return 0;
}
