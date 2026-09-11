#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/cervus.h>

#define DRV_OP_LIST      0
#define DRV_OP_START     1
#define DRV_OP_STOP      2
#define DRV_OP_AUTOSTART 3

#define DRV_MAX 64

#define ST_UNUSED  0
#define ST_FAILED  1
#define ST_RUNNING 2
#define ST_STOPPED 3

typedef struct __attribute__((packed)) {
    char     name[24];
    uint8_t  state;
    uint8_t  autostart;
    uint8_t  can_stop;
    uint8_t  _pad;
    uint16_t matched;
    uint16_t vendor;
    uint16_t device;
    uint16_t _pad2;
    uint64_t started_ns;
} drv_info_t;

static const char USAGE[] =
"drv - the device drivers this kernel carries\n"
"\n"
"Usage\n"
"  drv                      what is loaded, and what it found\n"
"  drv start <name>         probe the hardware again with that driver\n"
"  drv stop <name>          shut it down, where the driver can\n"
"  drv autostart <name> on|off\n"
"\n"
"A driver is listed as running once it has claimed at least one device.\n"
"'idle' means the driver is built in but nothing here matches it, which\n"
"is the normal state for most of them. 'failed' means it matched a\n"
"device and could not bring it up - that is the one worth looking into,\n"
"with dmesg or dbgmon.\n";

static void fmt_uptime(uint64_t started_ns, char *out, size_t cap) {
    if (started_ns == 0) { snprintf(out, cap, "%8s", "-"); return; }
    uint64_t now = cervus_uptime_ns();
    if (now <= started_ns) { snprintf(out, cap, "%8s", "0s"); return; }
    uint64_t s = (now - started_ns) / 1000000000ull;
    if (s < 60)        snprintf(out, cap, "%6llus", (unsigned long long)s);
    else if (s < 3600) snprintf(out, cap, "%3llum%02llus",
                                (unsigned long long)(s / 60), (unsigned long long)(s % 60));
    else               snprintf(out, cap, "%3lluh%02llum",
                                (unsigned long long)(s / 3600),
                                (unsigned long long)((s % 3600) / 60));
}

static const char *state_text(const drv_info_t *d, const char **colour) {
    switch (d->state) {
        case ST_RUNNING: *colour = "\x1b[32m"; return "running";
        case ST_FAILED:  *colour = "\x1b[31m"; return "failed";
        case ST_STOPPED: *colour = "\x1b[33m"; return "stopped";
        default:         *colour = "\x1b[90m"; return "idle";
    }
}

static int list_drivers(void) {
    static drv_info_t d[DRV_MAX];
    long n = syscall3(SYS_DRIVER_CTL, DRV_OP_LIST, (long)d, 0);
    if (n < 0) { fprintf(stderr, "drv: cannot read the driver table\n"); return 1; }

    printf("%-12s %-9s %-6s %-9s %-10s %s\n",
           "driver", "state", "auto", "uptime", "device", "");
    printf("\x1b[90m");
    for (int i = 0; i < 58; i++) putchar('-');
    printf("\x1b[0m\n");

    int running = 0, failed = 0;
    for (long i = 0; i < n; i++) {
        const char *col;
        const char *st = state_text(&d[i], &col);
        if (d[i].state == ST_RUNNING) running++;
        if (d[i].state == ST_FAILED)  failed++;

        char up[16];
        fmt_uptime(d[i].started_ns, up, sizeof up);

        char dev[24];
        if (d[i].matched) snprintf(dev, sizeof dev, "%04x:%04x", d[i].vendor, d[i].device);
        else              snprintf(dev, sizeof dev, "%s", "-");

        printf("%-12s %s%-9s\x1b[0m %-6s %-9s %-10s",
               d[i].name, col, st,
               d[i].autostart ? "yes" : "no",
               up, dev);
        if (d[i].matched > 1) printf(" \x1b[90m(%u devices)\x1b[0m", d[i].matched);
        if (d[i].state == ST_RUNNING && !d[i].can_stop)
            printf(" \x1b[90mno stop\x1b[0m");
        putchar('\n');
    }

    printf("\n%d running", running);
    if (failed) printf(", \x1b[31m%d failed\x1b[0m", failed);
    printf(", %ld built in\n", n);
    if (failed) printf("run 'dmesg' to see why a driver failed\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return list_drivers();
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) { fputs(USAGE, stdout); return 0; }
    if (!strcmp(argv[1], "list")) return list_drivers();

    if (argc < 3) { fputs(USAGE, stderr); return 1; }

    long r;
    if (!strcmp(argv[1], "start")) {
        r = syscall3(SYS_DRIVER_CTL, DRV_OP_START, (long)argv[2], 0);
        if (r == 0)      { printf("drv: %s claimed its device\n", argv[2]); return 0; }
        if (r == -2)     { fprintf(stderr, "drv: nothing here matches %s\n", argv[2]); return 1; }
        if (r == -3)     { fprintf(stderr, "drv: %s found its device but could not bring it up;"
                                           " dmesg says why\n", argv[2]); return 1; }
        fprintf(stderr, "drv: no driver called %s\n", argv[2]);
        return 1;
    }
    if (!strcmp(argv[1], "stop")) {
        r = syscall3(SYS_DRIVER_CTL, DRV_OP_STOP, (long)argv[2], 0);
        if (r == 0)  { printf("drv: %s stopped\n", argv[2]); return 0; }
        if (r == -2) { fprintf(stderr, "drv: %s has no way to shut down;"
                                       " it stays until the next boot\n", argv[2]); return 1; }
        if (r == -3) { fprintf(stderr, "drv: %s refused to stop\n", argv[2]); return 1; }
        fprintf(stderr, "drv: no driver called %s\n", argv[2]);
        return 1;
    }
    if (!strcmp(argv[1], "autostart")) {
        if (argc < 4) { fputs(USAGE, stderr); return 1; }
        int on = !strcmp(argv[3], "on") || !strcmp(argv[3], "yes") || !strcmp(argv[3], "1");
        r = syscall3(SYS_DRIVER_CTL, DRV_OP_AUTOSTART, (long)argv[2], on);
        if (r != 0) { fprintf(stderr, "drv: no driver called %s\n", argv[2]); return 1; }
        printf("drv: %s will %s be probed at boot\n", argv[2], on ? "" : "not ");
        return 0;
    }

    fputs(USAGE, stderr);
    return 1;
}
