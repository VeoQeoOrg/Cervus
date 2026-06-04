#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: top\nInteractive process viewer (top-style).\n"
    "Keys: q quit  P sort %CPU  M sort %MEM  N sort PID  T sort TIME  p pause\n";

#define TIOCGWINSZ     0x5413
#define TIOCSNONBLOCK  0x5481

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } winsize_t;

#define MAX_TASKS 256

typedef struct {
    uint32_t pid, ppid, uid, state, priority;
    uint64_t runtime_ns, prev_runtime_ns, cpu_ns_delta, virt_bytes, rss_bytes;
    uint64_t create_time_ns;
    char     name[32];
    int      seen;
} prow_t;

static struct termios g_orig_tio;
static int g_in_raw = 0, g_nonblock = 0;
static int g_cols = 80, g_rows_term = 24;
static int g_running = 1, g_paused = 0, g_sort = 0;
static int g_refresh_ms = 100, g_ncpus = 1;

static prow_t g_rows[MAX_TASKS];
static int g_nrows = 0;
static uint64_t g_prev_uptime_ns = 0, g_last_interval_ns = 0;
static cervus_meminfo_t g_mi = {0};
static int g_mi_valid = 0;
static unsigned g_cpu_pct_x100 = 0;

static int detect_ncpus(void) {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(1), "2"(0));
    int n = (b >> 16) & 0xFF;
    return n < 1 ? 1 : n;
}

static int tty_set_nonblock(int on) {
    int v = on;
    return (int)syscall3(SYS_IOCTL, 0, TIOCSNONBLOCK, &v);
}

static void restore_term(void) {
    if (g_nonblock) { tty_set_nonblock(0); g_nonblock = 0; }
    if (g_in_raw)   { tcsetattr(0, TCSAFLUSH, &g_orig_tio); g_in_raw = 0; }
    fputs("\x1b[?25h\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

static void enter_raw(void) {
    if (tcgetattr(0, &g_orig_tio) < 0) return;
    struct termios raw = g_orig_tio;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |=  (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &raw) == 0) g_in_raw = 1;
    if (tty_set_nonblock(1) == 0) g_nonblock = 1;
    fputs("\x1b[?25l\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

static void update_size(void) {
    winsize_t ws;
    if (syscall3(SYS_IOCTL, 1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 16 && ws.ws_row >= 8) {
        g_cols = ws.ws_col;
        g_rows_term = ws.ws_row;
    }
}

static int read_byte(unsigned char *out) {
    unsigned char c;
    if (read(0, &c, 1) == 1) { *out = c; return 1; }
    return 0;
}

static int read_key(int *out) {
    unsigned char c;
    if (!read_byte(&c)) return 0;
    if (c == 0x1B) {
        unsigned char s1, s2;
        for (int i = 0; i < 50 && !read_byte(&s1); i++) cervus_nanosleep(1000000ULL);
        if (s1 != '[' && s1 != 'O') { *out = 0x1B; return 1; }
        for (int i = 0; i < 50 && !read_byte(&s2); i++) cervus_nanosleep(1000000ULL);
        *out = 0x1B;
        return 1;
    }
    *out = c;
    return 1;
}

static uint64_t ns_now(void) { return cervus_uptime_ns(); }

static int read_proc_field(uint32_t pid, const char *file, const char *key,
                           char *out, int outsz) {
    char path[64], buf[1024];
    snprintf(path, sizeof(path), "/proc/%u/%s", pid, file);
    int fd = open(path, 0, 0);
    if (fd < 0) return -1;
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    size_t kl = strlen(key);
    char *p = buf;
    while (*p) {
        if (strncmp(p, key, kl) == 0) {
            p += kl;
            while (*p == ' ' || *p == '\t') p++;
            int i = 0;
            while (p[i] && p[i] != '\n' && p[i] != ' ' && i < outsz - 1) { out[i] = p[i]; i++; }
            out[i] = '\0';
            return 0;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return -1;
}

static void sample(uint64_t now) {
    cervus_meminfo_t mi;
    if (cervus_meminfo(&mi) == 0 && mi.total_bytes > 0) { g_mi = mi; g_mi_valid = 1; }

    for (int i = 0; i < g_nrows; i++) g_rows[i].seen = 0;
    int nr = g_nrows;

    cervus_task_info_t ti;
    for (uint32_t pid = 1; pid < 1024; pid++) {
        if (cervus_task_info((pid_t)pid, &ti) < 0) continue;
        int idx = -1;
        for (int i = 0; i < nr; i++) if (g_rows[i].pid == ti.pid) { idx = i; break; }
        if (idx < 0) {
            if (nr >= MAX_TASKS) continue;
            idx = nr++;
            memset(&g_rows[idx], 0, sizeof(g_rows[idx]));
            g_rows[idx].pid = ti.pid;
            g_rows[idx].prev_runtime_ns = ti.total_runtime_ns;
        }
        prow_t *r = &g_rows[idx];
        r->ppid = ti.ppid;
        r->uid  = ti.uid;
        r->state = ti.state;
        r->priority = ti.priority;
        r->cpu_ns_delta = ti.total_runtime_ns > r->runtime_ns
                        ? ti.total_runtime_ns - r->runtime_ns : 0;
        r->prev_runtime_ns = r->runtime_ns;
        r->runtime_ns = ti.total_runtime_ns;
        r->rss_bytes = ti.rss_bytes;
        r->create_time_ns = ti.create_time_ns;

        char vsz[32];
        if (read_proc_field(ti.pid, "status", "VmSize:", vsz, sizeof(vsz)) == 0)
            r->virt_bytes = (uint64_t)atoll(vsz) * 1024;
        else
            r->virt_bytes = ti.rss_bytes;

        strncpy(r->name, ti.name, sizeof(r->name) - 1);
        r->name[sizeof(r->name) - 1] = '\0';
        r->seen = 1;
    }

    int w = 0;
    for (int i = 0; i < nr; i++) {
        if (!g_rows[i].seen) continue;
        if (w != i) g_rows[w] = g_rows[i];
        w++;
    }
    g_nrows = w;

    uint64_t interval = (g_prev_uptime_ns > 0 && now > g_prev_uptime_ns)
                      ? (now - g_prev_uptime_ns) : 1;
    g_last_interval_ns = interval;
    uint64_t sum = 0;
    for (int i = 0; i < g_nrows; i++) sum += g_rows[i].cpu_ns_delta;
    uint64_t denom = (uint64_t)g_ncpus * interval;
    uint64_t p = denom ? (sum * 10000ULL) / denom : 0;
    g_cpu_pct_x100 = p > 10000 ? 10000 : (unsigned)p;
    g_prev_uptime_ns = now;
}

static unsigned row_cpu_x100(prow_t *r) {
    if (g_last_interval_ns == 0) return 0;
    uint64_t denom = (uint64_t)g_ncpus * g_last_interval_ns;
    uint64_t p = denom ? (r->cpu_ns_delta * 10000ULL) / denom : 0;
    return p > 10000 ? 10000 : (unsigned)p;
}

static unsigned row_mem_x100(prow_t *r) {
    if (!g_mi_valid || g_mi.total_bytes == 0) return 0;
    uint64_t p = (r->rss_bytes * 10000ULL) / g_mi.total_bytes;
    return p > 10000 ? 10000 : (unsigned)p;
}

static int cmp_cpu(const void *a, const void *b) {
    const prow_t *x = a, *y = b;
    if (x->cpu_ns_delta != y->cpu_ns_delta) return y->cpu_ns_delta > x->cpu_ns_delta ? 1 : -1;
    return (int)x->pid - (int)y->pid;
}
static int cmp_mem(const void *a, const void *b) {
    const prow_t *x = a, *y = b;
    if (x->rss_bytes != y->rss_bytes) return y->rss_bytes > x->rss_bytes ? 1 : -1;
    return (int)x->pid - (int)y->pid;
}
static int cmp_pid(const void *a, const void *b) {
    return (int)((const prow_t *)a)->pid - (int)((const prow_t *)b)->pid;
}
static int cmp_time(const void *a, const void *b) {
    const prow_t *x = a, *y = b;
    if (x->runtime_ns != y->runtime_ns) return y->runtime_ns > x->runtime_ns ? 1 : -1;
    return (int)x->pid - (int)y->pid;
}

static void sort_rows(void) {
    int (*cmp)(const void *, const void *) = cmp_cpu;
    if (g_sort == 1) cmp = cmp_mem;
    else if (g_sort == 2) cmp = cmp_pid;
    else if (g_sort == 3) cmp = cmp_time;
    qsort(g_rows, g_nrows, sizeof(prow_t), cmp);
}

static const char *state_letter(uint32_t s) {
    switch (s) {
        case 0: return "R";
        case 1: return "R";
        case 2: return "S";
        case 3: return "Z";
        default: return "D";
    }
}

static void fmt_kb(uint64_t kb, char *out, int n) {
    if (kb < 100000ULL)
        snprintf(out, n, "%llu", (unsigned long long)kb);
    else if (kb < 100ULL * 1024 * 1024)
        snprintf(out, n, "%llum", (unsigned long long)(kb / 1024));
    else
        snprintf(out, n, "%llug", (unsigned long long)(kb / (1024 * 1024)));
}

static void fmt_time(uint64_t ns, char *out, int n) {
    uint64_t cs = ns / 10000000ULL;
    uint64_t min = cs / 6000;
    uint64_t sec = (cs / 100) % 60;
    uint64_t hund = cs % 100;
    if (min < 10000)
        snprintf(out, n, "%llu:%02llu.%02llu",
                 (unsigned long long)min, (unsigned long long)sec, (unsigned long long)hund);
    else {
        uint64_t hr = min / 60;
        snprintf(out, n, "%lluh%02llum",
                 (unsigned long long)hr, (unsigned long long)(min % 60));
    }
}

static void fmt_elapsed(uint64_t ns, char *out, int n) {
    uint64_t s = ns / 1000000000ULL;
    uint64_t d = s / 86400;
    uint64_t hh = (s / 3600) % 24;
    uint64_t mm = (s / 60) % 60;
    uint64_t ss = s % 60;
    if (d > 0)
        snprintf(out, n, "%llud%02lluh", (unsigned long long)d, (unsigned long long)hh);
    else if (hh > 0)
        snprintf(out, n, "%llu:%02llu:%02llu",
                 (unsigned long long)hh, (unsigned long long)mm, (unsigned long long)ss);
    else
        snprintf(out, n, "%llu:%02llu", (unsigned long long)mm, (unsigned long long)ss);
}

static void draw(uint64_t up_ns) {
    fputs("\x1b[?2026h\x1b[H", stdout);

    uint64_t s = up_ns / 1000000000ULL;
    uint64_t up_d = s / 86400, up_h = (s / 3600) % 24, up_m = (s / 60) % 60;

    int running = 0, sleeping = 0, zombie = 0;
    for (int i = 0; i < g_nrows; i++) {
        if (g_rows[i].state == 0 || g_rows[i].state == 1) running++;
        else if (g_rows[i].state == 3) zombie++;
        else sleeping++;
    }

    char upbuf[64];
    if (up_d > 0)
        snprintf(upbuf, sizeof(upbuf), "%llu day%s, %2llu:%02llu",
                 (unsigned long long)up_d, up_d == 1 ? "" : "s",
                 (unsigned long long)up_h, (unsigned long long)up_m);
    else
        snprintf(upbuf, sizeof(upbuf), "%2llu:%02llu", (unsigned long long)up_h, (unsigned long long)up_m);

    printf("\x1b[1mtop\x1b[0m - up %s,  %d cpu,  load %u.%02u%%\x1b[K\r\n",
           upbuf, g_ncpus, g_cpu_pct_x100 / 100, g_cpu_pct_x100 % 100);

    printf("Tasks: \x1b[1m%d\x1b[0m total, \x1b[1m%d\x1b[0m running, \x1b[1m%d\x1b[0m sleeping, "
           "\x1b[1m%d\x1b[0m zombie\x1b[K\r\n",
           g_nrows, running, sleeping, zombie);

    unsigned busy = g_cpu_pct_x100;
    unsigned idle = busy < 10000 ? 10000 - busy : 0;
    printf("%%Cpu(s): \x1b[1m%3u.%01u\x1b[0m us, \x1b[1m%3u.%01u\x1b[0m id\x1b[K\r\n",
           busy / 100, (busy % 100) / 10, idle / 100, (idle % 100) / 10);

    uint64_t tot_mib = 0, free_mib = 0, used_mib = 0;
    unsigned mem_pct = 0;
    if (g_mi_valid && g_mi.total_bytes) {
        tot_mib = g_mi.total_bytes / (1024 * 1024);
        free_mib = g_mi.free_bytes / (1024 * 1024);
        used_mib = g_mi.used_bytes / (1024 * 1024);
        uint64_t p = (g_mi.used_bytes * 10000ULL) / g_mi.total_bytes;
        mem_pct = p > 10000 ? 10000 : (unsigned)p;
    }
    printf("MiB Mem : \x1b[1m%8llu\x1b[0m total, \x1b[1m%8llu\x1b[0m free, "
           "\x1b[1m%8llu\x1b[0m used  (%u.%01u%%)\x1b[K\r\n",
           (unsigned long long)tot_mib, (unsigned long long)free_mib,
           (unsigned long long)used_mib, mem_pct / 100, (mem_pct % 100) / 10);
    printf("MiB Swap: \x1b[1m%8d\x1b[0m total, \x1b[1m%8d\x1b[0m free, "
           "\x1b[1m%8d\x1b[0m used\x1b[K\r\n", 0, 0, 0);
    fputs("\x1b[K\r\n", stdout);

    fputs("\x1b[7m", stdout);
    int col = printf("  PID USER     PR NI     VIRT     RES S  %%CPU  %%MEM      TIME+   ELAPSED COMMAND");
    while (col < g_cols) { putchar(' '); col++; }
    fputs("\x1b[0m\x1b[K\r\n", stdout);

    int header_rows = 8;
    int visible = g_rows_term - header_rows - 1;
    if (visible < 1) visible = 1;

    for (int i = 0; i < visible; i++) {
        if (i >= g_nrows) { fputs("\x1b[K\r\n", stdout); continue; }
        prow_t *r = &g_rows[i];
        unsigned cpu = row_cpu_x100(r);
        unsigned mem = row_mem_x100(r);
        char tbuf[16], vbuf[12], rbuf[12], cbuf[8], mbuf[8], ubuf[12], nbuf[24], ebuf[16];
        fmt_kb(r->virt_bytes / 1024, vbuf, sizeof(vbuf));
        fmt_kb(r->rss_bytes / 1024, rbuf, sizeof(rbuf));
        fmt_time(r->runtime_ns, tbuf, sizeof(tbuf));
        uint64_t elapsed = (up_ns > r->create_time_ns) ? (up_ns - r->create_time_ns) : 0;
        fmt_elapsed(elapsed, ebuf, sizeof(ebuf));
        snprintf(cbuf, sizeof(cbuf), "%u.%01u", cpu / 100, (cpu % 100) / 10);
        snprintf(mbuf, sizeof(mbuf), "%u.%01u", mem / 100, (mem % 100) / 10);
        snprintf(ubuf, sizeof(ubuf), "%-8.8s", (r->uid == 0) ? "root" : "user");
        snprintf(nbuf, sizeof(nbuf), "%-.20s", r->name);
        const char *sl = state_letter(r->state);
        const char *hot = (cpu >= 5000) ? "\x1b[1m" : "";
        const char *hot_end = (cpu >= 5000) ? "\x1b[0m" : "";

        printf("%s%5u %s %2d  0 %8s %7s %s %5s %5s %10s %9s %s%s\x1b[K\r\n",
               hot,
               (unsigned)r->pid, ubuf,
               (int)r->priority,
               vbuf, rbuf, sl,
               cbuf, mbuf, tbuf, ebuf, nbuf,
               hot_end);
    }

    fputs("\x1b[J", stdout);
    if (g_paused) {
        fputs("\x1b[7m [PAUSED] \x1b[0m", stdout);
    }
    fputs("\x1b[?2026l", stdout);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (cervus_check_help_version(argc, argv, USAGE, "top")) return 0;
    (void)argc; (void)argv;

    g_ncpus = detect_ncpus();
    update_size();
    enter_raw();

    g_prev_uptime_ns = ns_now();
    sample(g_prev_uptime_ns);
    for (int i = 0; i < g_nrows; i++) g_rows[i].cpu_ns_delta = 0;
    sort_rows();
    draw(ns_now());

    uint64_t next = ns_now() + (uint64_t)g_refresh_ms * 1000000ULL;

    while (g_running) {
        int key, redraw = 0, resort = 0;
        while (read_key(&key)) {
            redraw = 1;
            if (key == 'q' || key == 'Q' || key == 3) { g_running = 0; break; }
            else if (key == 'P') { g_sort = 0; resort = 1; }
            else if (key == 'M') { g_sort = 1; resort = 1; }
            else if (key == 'N') { g_sort = 2; resort = 1; }
            else if (key == 'T') { g_sort = 3; resort = 1; }
            else if (key == ' ') { }
            else if (key == 'p') { g_paused = !g_paused; }
        }
        if (resort) sort_rows();
        if (redraw) { update_size(); draw(ns_now()); }

        uint64_t now = ns_now();
        if (!g_paused && now >= next) {
            sample(now);
            sort_rows();
            update_size();
            draw(now);
            next = now + (uint64_t)g_refresh_ms * 1000000ULL;
        }
        cervus_nanosleep(15000000ULL);
    }

    restore_term();
    return 0;
}
