#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <cervus_util.h>

#define TIOCGWINSZ     0x5413
#define TIOCSNONBLOCK  0x5481

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } winsize_t;

#define MAX_TASKS 256

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t state;
    uint32_t priority;
    uint64_t runtime_ns;
    uint64_t prev_runtime_ns;
    uint64_t cpu_ns_delta;
    uint64_t rss_bytes;
    uint64_t create_time_ns;
    char     name[32];
    int      seen;
} task_row_t;

static struct termios g_orig_tio;
static int g_in_raw = 0;
static int g_cols = 80;
static int g_term_rows = 24;
static int g_running = 1;
static int g_paused  = 0;
static int g_sort = 0;
static int g_selected = 0;
static int g_scroll = 0;
static int g_refresh_ms = 100;
static int g_show_help = 0;

static task_row_t g_rows[MAX_TASKS];
static int g_nrows = 0;
static int g_ncpus = 1;
static uint64_t g_prev_total_ns = 0;
static uint64_t g_prev_uptime_ns = 0;
static unsigned g_cpu_pct_x100 = 0;
static uint64_t g_last_interval_ns = 0;
static cervus_meminfo_t g_mi = {0};
static int g_mi_valid = 0;

static int detect_ncpus(void)
{
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                          : "0"(1), "2"(0));
    int logical = (b >> 16) & 0xFF;
    if (logical < 1) logical = 1;
    return logical;
}

static int g_stdin_nonblock = 0;

static int tty_set_nonblock(int on)
{
    int v = on;
    return (int)syscall3(SYS_IOCTL, 0, TIOCSNONBLOCK, &v);
}

static void restore_term(void)
{
    fputs("\x1b[?25h\x1b[0m\x1b[2J\x1b[H", stdout);
    fflush(stdout);
    if (g_stdin_nonblock) {
        tty_set_nonblock(0);
        g_stdin_nonblock = 0;
    }
    if (g_in_raw) {
        tcsetattr(0, TCSAFLUSH, &g_orig_tio);
        g_in_raw = 0;
    }
    fputs("\x1b[?25h", stdout);
    fflush(stdout);
}

static void enter_raw(void)
{
    if (tcgetattr(0, &g_orig_tio) < 0) return;
    struct termios raw = g_orig_tio;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |=  (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &raw) == 0) g_in_raw = 1;

    if (tty_set_nonblock(1) == 0) g_stdin_nonblock = 1;

    fputs("\x1b[?25l\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

static void update_size(void)
{
    winsize_t ws;
    if (syscall3(SYS_IOCTL, 1, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col >= 16 && ws.ws_row >= 8) {
        g_cols = ws.ws_col;
        g_term_rows = ws.ws_row;
    }
}

static int read_byte_nb(unsigned char *out)
{
    unsigned char c;
    ssize_t n = read(0, &c, 1);
    if (n == 1) { *out = c; return 1; }
    return 0;
}

static int read_key_nonblock(int *out)
{
    unsigned char c;
    if (!read_byte_nb(&c)) return 0;
    if (c == 0x1B) {
        unsigned char s1, s2;
        for (int i = 0; i < 100 && !read_byte_nb(&s1); i++) cervus_nanosleep(1000000ULL);
        if (s1 != '[') { *out = 0x1B; return 1; }
        for (int i = 0; i < 100 && !read_byte_nb(&s2); i++) cervus_nanosleep(1000000ULL);
        switch (s2) {
            case 'A': *out = 1000; return 1;
            case 'B': *out = 1001; return 1;
            case 'C': *out = 1002; return 1;
            case 'D': *out = 1003; return 1;
            case '5': { unsigned char z; (void)read_byte_nb(&z); *out = 1004; return 1; }
            case '6': { unsigned char z; (void)read_byte_nb(&z); *out = 1005; return 1; }
            default:  *out = 0x1B; return 1;
        }
    }
    *out = c;
    return 1;
}

static void sample_mem(void)
{
    cervus_meminfo_t mi;
    memset(&mi, 0, sizeof(mi));
    int rc = cervus_meminfo(&mi);
    if (rc == 0 && mi.total_bytes > 0) {
        g_mi = mi;
        g_mi_valid = 1;
    }
}

static const char *state_name(uint32_t s)
{
    switch (s) {
        case 0: return "RUN";
        case 1: return "RDY";
        case 2: return "BLK";
        case 3: return "ZMB";
        default: return "?  ";
    }
}

static const char *state_color(uint32_t s)
{
    switch (s) {
        case 0: return "\x1b[1;32m";
        case 1: return "\x1b[1;36m";
        case 2: return "\x1b[1;33m";
        case 3: return "\x1b[1;31m";
        default: return "\x1b[0m";
    }
}

static void sample_tasks(uint64_t cur_uptime_ns) {
    for (int i = 0; i < g_nrows; i++) g_rows[i].seen = 0;

    int new_nrows = g_nrows;

    cervus_task_info_t info;
    for (pid_t pid = 0; pid < 1024; pid++) {
        if (cervus_task_info(pid, &info) < 0) continue;

        int idx = -1;
        for (int i = 0; i < new_nrows; i++) {
            if (g_rows[i].pid == info.pid) { idx = i; break; }
        }
        if (idx < 0) {
            if (new_nrows >= MAX_TASKS) continue;
            idx = new_nrows++;
            memset(&g_rows[idx], 0, sizeof(g_rows[idx]));
            g_rows[idx].pid = info.pid;
            g_rows[idx].prev_runtime_ns = info.total_runtime_ns;
        }
        task_row_t *r = &g_rows[idx];
        r->ppid = info.ppid;
        r->uid  = info.uid;
        r->state = info.state;
        r->priority = info.priority;
        r->cpu_ns_delta = info.total_runtime_ns > r->runtime_ns
                          ? info.total_runtime_ns - r->runtime_ns : 0;
        r->prev_runtime_ns = r->runtime_ns;
        r->runtime_ns = info.total_runtime_ns;
        r->rss_bytes = info.rss_bytes;
        r->create_time_ns = info.create_time_ns;
        strncpy(r->name, info.name, sizeof(r->name) - 1);
        r->name[sizeof(r->name) - 1] = '\0';
        r->seen = 1;
    }

    int w = 0;
    for (int i = 0; i < new_nrows; i++) {
        if (!g_rows[i].seen) continue;
        if (w != i) g_rows[w] = g_rows[i];
        w++;
    }
    g_nrows = w;

    uint64_t sum = 0;
    for (int i = 0; i < g_nrows; i++) sum += g_rows[i].cpu_ns_delta;
    uint64_t interval = (g_prev_uptime_ns > 0 && cur_uptime_ns > g_prev_uptime_ns)
                       ? (cur_uptime_ns - g_prev_uptime_ns) : 1;
    g_last_interval_ns = interval;
    uint64_t denom = (uint64_t)g_ncpus * interval;
    uint64_t pct_x100 = denom ? (sum * 10000ULL) / denom : 0;
    if (pct_x100 > 10000) pct_x100 = 10000;
    g_cpu_pct_x100 = (unsigned)pct_x100;
    g_prev_uptime_ns = cur_uptime_ns;
    g_prev_total_ns  = sum;
}

static int cmp_cpu(const void *a, const void *b)
{
    const task_row_t *x = a, *y = b;
    if (x->cpu_ns_delta != y->cpu_ns_delta)
        return (y->cpu_ns_delta > x->cpu_ns_delta) ? 1 : -1;
    return (int)x->pid - (int)y->pid;
}
static int cmp_runtime(const void *a, const void *b)
{
    const task_row_t *x = a, *y = b;
    if (x->runtime_ns != y->runtime_ns)
        return (y->runtime_ns > x->runtime_ns) ? 1 : -1;
    return (int)x->pid - (int)y->pid;
}
static int cmp_pid(const void *a, const void *b)
{
    const task_row_t *x = a, *y = b;
    return (int)x->pid - (int)y->pid;
}
static int cmp_name(const void *a, const void *b)
{
    const task_row_t *x = a, *y = b;
    return strcmp(x->name, y->name);
}
static int cmp_mem(const void *a, const void *b)
{
    const task_row_t *x = a, *y = b;
    if (x->rss_bytes != y->rss_bytes)
        return (y->rss_bytes > x->rss_bytes) ? 1 : -1;
    return (int)x->pid - (int)y->pid;
}

static void sort_rows(void)
{
    int (*cmp)(const void *, const void *) = cmp_cpu;
    if (g_sort == 1) cmp = cmp_runtime;
    else if (g_sort == 2) cmp = cmp_pid;
    else if (g_sort == 3) cmp = cmp_name;
    else if (g_sort == 4) cmp = cmp_mem;
    qsort(g_rows, g_nrows, sizeof(task_row_t), cmp);
}

static void fmt_bytes(uint64_t b, char *out, size_t n)
{
    const uint64_t GiB = 1024ULL * 1024 * 1024;
    const uint64_t MiB = 1024ULL * 1024;
    const uint64_t KiB = 1024ULL;
    if (b >= GiB) snprintf(out, n, "%llu.%02llu GiB",
                          (unsigned long long)(b / GiB),
                          (unsigned long long)((b % GiB) * 100 / GiB));
    else if (b >= MiB) snprintf(out, n, "%llu.%02llu MiB",
                          (unsigned long long)(b / MiB),
                          (unsigned long long)((b % MiB) * 100 / MiB));
    else if (b >= KiB) snprintf(out, n, "%llu KiB", (unsigned long long)(b / KiB));
    else snprintf(out, n, "%llu B", (unsigned long long)b);
}

static void fmt_runtime(uint64_t ns, char *out, size_t n)
{
    uint64_t cs = ns / 10000000ULL;
    uint64_t hh = cs / 360000ULL;
    if (hh > 0) {
        uint64_t mm = (cs / 6000ULL) % 60;
        snprintf(out, n, "%lluh%02llum", (unsigned long long)hh, (unsigned long long)mm);
    } else {
        uint64_t mm = cs / 6000ULL;
        uint64_t ss = (cs / 100ULL) % 60;
        uint64_t hund = cs % 100;
        snprintf(out, n, "%llu:%02llu.%02llu",
                 (unsigned long long)mm, (unsigned long long)ss, (unsigned long long)hund);
    }
}

static void fmt_elapsed(uint64_t ns, char *out, size_t n)
{
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

static void draw_bar(int width, unsigned pct_x100, const char *color)
{
    if (width < 4) return;
    if (pct_x100 > 10000) pct_x100 = 10000;
    int fill = (int)(((uint64_t)pct_x100 * (uint64_t)(width - 2)) / 10000ULL);
    fputs("[", stdout);
    fputs(color, stdout);
    for (int i = 0; i < width - 2; i++) {
        if (i < fill) fputc('|', stdout);
        else fputc(' ', stdout);
    }
    fputs(C_RESET, stdout);
    fputs("]", stdout);
}

static void draw_screen(uint64_t uptime_ns) {
    fputs("\x1b[?2026h\x1b[H", stdout);

    fputs(C_BOLD C_CYAN " Cervus sysmon " C_RESET, stdout);
    uint64_t s = uptime_ns / 1000000000ULL;
    uint64_t hh = s / 3600;
    uint64_t mm = (s / 60) % 60;
    uint64_t ss = s % 60;
    printf(" up %02llu:%02llu:%02llu  cpus:%d  procs:%d  ",
           (unsigned long long)hh, (unsigned long long)mm, (unsigned long long)ss,
           g_ncpus, g_nrows);
    if (g_paused) fputs(C_YELLOW "[PAUSED] " C_RESET, stdout);
    fputs("\x1b[K\r\n", stdout);

    int bar_w = g_cols - 14;
    if (bar_w < 10) bar_w = 10;
    if (bar_w > 60) bar_w = 60;
    printf(" CPU %3u.%02u%% ", g_cpu_pct_x100 / 100, g_cpu_pct_x100 % 100);
    const char *c_color = (g_cpu_pct_x100 < 5000) ? C_GREEN
                       : (g_cpu_pct_x100 < 8000 ? C_YELLOW : C_RED);
    draw_bar(bar_w, g_cpu_pct_x100, c_color);
    fputs("\x1b[K\r\n", stdout);

    char used_s[32] = "?", total_s[32] = "?";
    unsigned mem_pct_x100 = 0;
    if (g_mi_valid && g_mi.total_bytes > 0) {
        fmt_bytes(g_mi.used_bytes,  used_s,  sizeof(used_s));
        fmt_bytes(g_mi.total_bytes, total_s, sizeof(total_s));
        uint64_t p = (g_mi.used_bytes * 10000ULL) / g_mi.total_bytes;
        if (p > 10000) p = 10000;
        mem_pct_x100 = (unsigned)p;
    }
    printf(" MEM %3u.%02u%% ", mem_pct_x100 / 100, mem_pct_x100 % 100);
    const char *m_color = (mem_pct_x100 < 5000) ? C_GREEN
                       : (mem_pct_x100 < 8000 ? C_YELLOW : C_RED);
    draw_bar(bar_w, mem_pct_x100, m_color);
    printf(" %s / %s\x1b[K\r\n", used_s, total_s);

    fputs(C_BOLD, stdout);
    int line = printf("  PID  PPID  UID  STATE  PRIO  CPU%%       MEM  RUNTIME    ELAPSED  NAME");
    while (line < g_cols) { putchar(' '); line++; }
    fputs(C_RESET, stdout);
    fputs("\x1b[K\r\n", stdout);

    int reserved_rows = 5;
    int visible = g_term_rows - reserved_rows - 1;
    if (visible < 1) visible = 1;
    if (g_selected < 0) g_selected = 0;
    if (g_selected >= g_nrows) g_selected = g_nrows ? g_nrows - 1 : 0;

    if (g_selected < g_scroll) g_scroll = g_selected;
    if (g_selected >= g_scroll + visible) g_scroll = g_selected - visible + 1;
    if (g_scroll < 0) g_scroll = 0;
    int max_scroll = g_nrows > visible ? g_nrows - visible : 0;
    if (g_scroll > max_scroll) g_scroll = max_scroll;

    uint64_t interval = g_prev_uptime_ns > 0 ? 0 : 1;
    (void)interval;

    for (int i = 0; i < visible; i++) {
        int idx = g_scroll + i;
        if (idx >= g_nrows) {
            fputs("\x1b[K\r\n", stdout);
            continue;
        }
        task_row_t *r = &g_rows[idx];

        unsigned pct_x100 = 0;
        if (g_last_interval_ns > 0) {
            uint64_t denom = (uint64_t)g_ncpus * g_last_interval_ns;
            uint64_t p = denom ? (r->cpu_ns_delta * 10000ULL) / denom : 0;
            if (p > 10000) p = 10000;
            pct_x100 = (unsigned)p;
        }

        char rt[16];
        fmt_runtime(r->runtime_ns, rt, sizeof(rt));

        char el[16];
        uint64_t elapsed = (uptime_ns > r->create_time_ns) ? (uptime_ns - r->create_time_ns) : 0;
        fmt_elapsed(elapsed, el, sizeof(el));

        char mem[16];
        fmt_bytes(r->rss_bytes, mem, sizeof(mem));

        const char *scolor = state_color(r->state);
        const char *sname  = state_name(r->state);

        const char *sel_pre = (idx == g_selected) ? "\x1b[7m" : "";
        const char *sel_post = (idx == g_selected) ? C_RESET : "";

        printf("%s%5u %5u %4u  %s%s%s  %4u  %3u.%02u%% %9s %8s %10s  %-.32s%s",
               sel_pre,
               (unsigned)r->pid, (unsigned)r->ppid, (unsigned)r->uid,
               scolor, sname, C_RESET,
               (unsigned)r->priority,
               pct_x100 / 100, pct_x100 % 100,
               mem,
               rt,
               el,
               r->name,
               sel_post);
        fputs("\x1b[K\r\n", stdout);
    }

    fputs("\x1b[", stdout);
    printf("%d;1H", g_term_rows);
    fputs(C_BOLD C_CYAN, stdout);
    if (g_show_help) {
        fputs(" [q]uit  [k]ill  [p]ause  [s]ort: c=cpu r=run i=pid n=name m=mem  [?]hide help", stdout);
    } else {
        const char *names[] = {"cpu", "runtime", "pid", "name", "mem"};
        printf(" sort=%s  rows=%d/%d  press [?] for help",
               names[g_sort], g_nrows, g_nrows);
    }
    fputs(C_RESET "\x1b[K\x1b[?2026l", stdout);
    fflush(stdout);
}

static int input_line_at(int row, const char *prompt, char *buf, int bufsz) {
    fputs("\x1b[", stdout);
    printf("%d;1H", row);
    fputs("\x1b[K", stdout);
    fputs(C_YELLOW, stdout);
    fputs(prompt, stdout);
    fputs(C_RESET, stdout);
    fputs("\x1b[?25h", stdout);
    fflush(stdout);
    int n = 0;
    while (n < bufsz - 1) {
        unsigned char c;
        while (!read_byte_nb(&c)) cervus_nanosleep(20000000ULL);
        if (c == '\r' || c == '\n') break;
        if (c == 27) { n = 0; break; }
        if (c == 127 || c == 8) {
            if (n > 0) { n--; fputs("\b \b", stdout); fflush(stdout); }
            continue;
        }
        buf[n++] = (char)c;
        putchar((char)c);
        fflush(stdout);
    }
    buf[n] = '\0';
    fputs("\x1b[?25l", stdout);
    fflush(stdout);
    return n;
}

static uint64_t ns_now(void)
{
    return cervus_uptime_ns();
}

static void on_sig(int s) { (void)s; g_running = 0; }

static void sysmon_snapshot(void) {
    g_ncpus = detect_ncpus();
    sample_mem();
    g_prev_uptime_ns = 0;
    sample_tasks(ns_now());
    sort_rows();

    uint64_t s = ns_now() / 1000000000ULL;
    printf("Cervus sysmon snapshot  up %02llu:%02llu:%02llu  cpus:%d  procs:%d\n",
           (unsigned long long)(s / 3600),
           (unsigned long long)((s / 60) % 60),
           (unsigned long long)(s % 60), g_ncpus, g_nrows);

    if (g_mi_valid && g_mi.total_bytes > 0) {
        char us[32], ts[32];
        fmt_bytes(g_mi.used_bytes,  us, sizeof(us));
        fmt_bytes(g_mi.total_bytes, ts, sizeof(ts));
        unsigned p = (unsigned)((g_mi.used_bytes * 10000ULL) / g_mi.total_bytes);
        printf("MEM %u.%02u%%  %s / %s\n", p / 100, p % 100, us, ts);
    }

    printf("  PID  PPID  UID  STATE  PRIO       MEM  RUNTIME  NAME\n");
    for (int i = 0; i < g_nrows; i++) {
        task_row_t *r = &g_rows[i];
        char rt[16];
        fmt_runtime(r->runtime_ns, rt, sizeof(rt));
        char mem[16];
        fmt_bytes(r->rss_bytes, mem, sizeof(mem));
        printf("%5u %5u %4u  %-5s  %4u  %9s  %8s  %s\n",
               (unsigned)r->pid, (unsigned)r->ppid, (unsigned)r->uid,
               state_name(r->state), (unsigned)r->priority, mem, rt, r->name);
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    static const char USAGE[] =
        "Usage: sysmon\n"
        "Interactive system monitor (CPU, memory, processes).\n"
        "\n"
        "Keys:\n"
        "  q       quit\n"
        "  k       kill selected process\n"
        "  p       pause/resume refresh\n"
        "  s       cycle sort (cpu/runtime/pid/name/mem)\n"
        "  m       sort by memory (RSS)\n"
        "  + -     change refresh rate\n"
        "  arrows  select process\n"
        "  ?       toggle help line\n";
    if (cervus_check_help_version(argc, argv, USAGE, "sysmon")) return 0;
    (void)argc; (void)argv;

    if (!isatty(0) || !isatty(1)) {
        sysmon_snapshot();
        return 0;
    }

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    g_ncpus = detect_ncpus();
    update_size();
    enter_raw();
    atexit(restore_term);

    sample_mem();
    g_prev_uptime_ns = ns_now();
    sample_tasks(g_prev_uptime_ns);
    for (int i = 0; i < g_nrows; i++) g_rows[i].cpu_ns_delta = 0;

    uint64_t next_sample = ns_now() + (uint64_t)g_refresh_ms * 1000000ULL;

    sort_rows();
    draw_screen(ns_now());

    while (g_running) {
        int key;
        int redraw = 0;
        int resort = 0;
        int got_key = 0;
        while (read_key_nonblock(&key)) {
            got_key = 1;
            redraw = 1;
            if (key == 'q' || key == 'Q' || key == 3) { g_running = 0; break; }
            else if (key == '?' || key == 'h' || key == 'H') g_show_help = !g_show_help;
            else if (key == 'p' || key == 'P') g_paused = !g_paused;
            else if (key == 's' || key == 'S') { g_sort = (g_sort + 1) % 5; resort = 1; }
            else if (key == 'c' || key == 'C') { g_sort = 0; resort = 1; }
            else if (key == 'r' || key == 'R') { g_sort = 1; resort = 1; }
            else if (key == 'i' || key == 'I') { g_sort = 2; resort = 1; }
            else if (key == 'n' || key == 'N') { g_sort = 3; resort = 1; }
            else if (key == 'm' || key == 'M') { g_sort = 4; resort = 1; }
            else if (key == 1000)    { if (g_selected > 0) g_selected--; }
            else if (key == 1001)  { if (g_selected + 1 < g_nrows) g_selected++; }
            else if (key == 1004)  { g_selected -= 10; if (g_selected < 0) g_selected = 0; }
            else if (key == 1005)  { g_selected += 10; if (g_selected >= g_nrows) g_selected = g_nrows - 1; }
            else if (key == 'k' || key == 'K') {
                if (g_selected >= 0 && g_selected < g_nrows) {
                    task_row_t *r = &g_rows[g_selected];
                    char prompt[128], reply[16];
                    snprintf(prompt, sizeof(prompt),
                             " Kill PID %u (%s)? [y/N]: ", (unsigned)r->pid, r->name);
                    input_line_at(g_term_rows, prompt, reply, sizeof(reply));
                    if (reply[0] == 'y' || reply[0] == 'Y') {
                        int rc = cervus_task_kill((pid_t)r->pid);
                        char msg[128];
                        snprintf(msg, sizeof(msg), " kill(%u) = %d",
                                 (unsigned)r->pid, rc);
                        fputs("\x1b[", stdout); printf("%d;1H", g_term_rows);
                        fputs("\x1b[K", stdout);
                        fputs(C_RED, stdout); fputs(msg, stdout); fputs(C_RESET, stdout);
                        fflush(stdout);
                        cervus_nanosleep(400000000ULL);
                    }
                }
            }
        }
        if (resort) sort_rows();
        if (redraw) draw_screen(ns_now());

        uint64_t now = ns_now();
        if (!g_paused && now >= next_sample) {
            sample_mem();
            sample_tasks(now);
            sort_rows();
            update_size();
            draw_screen(now);
            next_sample = now + (uint64_t)g_refresh_ms * 1000000ULL;
        }
        if (!got_key) cervus_nanosleep(10000000ULL);
    }

    restore_term();
    return 0;
}
